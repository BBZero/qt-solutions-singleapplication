// Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
// SPDX-License-Identifier: BSD-3-Clause


#include "qtlocalpeer.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QRegularExpression>
#include <QTime>

#if defined(Q_OS_WIN)
#include <QLibrary>
#include <qt_windows.h>
typedef BOOL(WINAPI*PProcessIdToSessionId)(DWORD,DWORD*);
static PProcessIdToSessionId pProcessIdToSessionId = 0;
#endif
#if defined(Q_OS_UNIX)
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#endif

namespace QtLP_Private {
#include "qtlockedfile.cpp"
#if defined(Q_OS_WIN)
#include "qtlockedfile_win.cpp"
#else
#include "qtlockedfile_unix.cpp"
#endif
}

const char* QtLocalPeer::ack = "ack";

QtLocalPeer::QtLocalPeer(QObject* parent, const QString &appId, const bool retryMode)
    : QObject(parent), mId(appId), mRetryMode(retryMode)
{
	if (!retryMode)
	{
		QString lockName;
		calParameterById(mId, mSocketName, lockName);

		m_pServer = new QLocalServer(this);
		mLockFile.setFileName(lockName);
		mLockFile.open(QIODevice::ReadWrite);
	}
	else {
		//do nothing
		//if retry, will ignore the appid in init
		mId = "";
		m_pServer = new QLocalServer(this);
	}
}
QtLocalPeer::~QtLocalPeer()
{
	if (m_pServer)
	{
		if (m_pServer->isListening())
		{
			m_pServer->close();
		}
		delete m_pServer;
		m_pServer = nullptr;
	}
}
QString QtLocalPeer::adjustIdAndGenPrefix(QString& srcId)
{
	QString prefix = srcId;
	if (srcId.isEmpty()) {
		srcId = QCoreApplication::applicationFilePath();
#if defined(Q_OS_WIN)
		srcId = srcId.toLower();
#endif
		prefix = srcId.section(QLatin1Char('/'), -1);
	}
	prefix.remove(QRegularExpression("[^a-zA-Z0-9]"));//old [^a-zA-Z]
	prefix.truncate(8);//org 6, we change to 8
	return prefix;
}
void QtLocalPeer::calParameterById(QString& srcId, QString& socketName, QString& lockfName)
{
	QString prefix = adjustIdAndGenPrefix(srcId);
	QByteArray idc = srcId.toUtf8();
	quint16 idNum = qChecksum(idc.constData(), idc.size());
	//org:qtsingleapp
	socketName = QLatin1String("qsingle-") + prefix
		+ QLatin1Char('-') + QString::number(idNum, 16, Qt::ChecksumIso3309);

#if defined(Q_OS_WIN)
	if (!pProcessIdToSessionId) {
		QLibrary lib("kernel32");
		pProcessIdToSessionId = (PProcessIdToSessionId)lib.resolve("ProcessIdToSessionId");
	}
	if (pProcessIdToSessionId) {
		DWORD sessionId = 0;
		pProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
		socketName += QLatin1Char('-') + QString::number(sessionId, 16);
	}
#else
	socketName += QLatin1Char('-') + QString::number(::getuid(), 16);
#endif

	lockfName = QDir(QDir::tempPath()).absolutePath()
		+ QLatin1Char('/') + socketName
		+ QLatin1String("-lock");//old: lockfile
}
bool QtLocalPeer::isIdEqual(const QString& srcId) const 
{
	QString _srcId = srcId;
	adjustIdAndGenPrefix(_srcId);
	return _srcId == mId;
}
bool QtLocalPeer::tryRunMaster()
{
	if (mId.isEmpty())
	{
		return false;
	}

	if (!mIsClient)
	{
		return true;
	}
	if (mLockFile.isLocked())
	{
		return true;
	}
	if (!mLockFile.lock(QtLP_Private::QtLockedFile::WriteLock, false))
	{
		return false;
	}
	if (m_pServer->isListening())
	{
		qWarning("QtLocalPeer::tryRunMaster() m_pServer->isListening()", qPrintable(m_pServer->fullServerName()));
		return true;
	}
	bool res = m_pServer->listen(mSocketName);
#if defined(Q_OS_UNIX) && (QT_VERSION >= QT_VERSION_CHECK(4,5,0))
	// ### Workaround to do
	if (!res && m_pServer->serverError() == QAbstractSocket::AddressInUseError) {
		QFile::remove(QDir::cleanPath(QDir::tempPath()) + QLatin1Char('/') + mSocketName);
		res = m_pServer->listen(mSocketName);
	}
#endif
	if (!res)
	{
		qWarning("QtSingleCoreApplication: listen on local socket failed, %s", qPrintable(m_pServer->errorString()));
	}
	QObject::connect(m_pServer, SIGNAL(newConnection()), SLOT(receiveConnection()));//, Qt::UniqueConnection
	return true;
}
QtLocalPeer::RunRes QtLocalPeer::retry(const QString &appId)
{
	if (!mIsClient)
	{
		qWarning("QtLocalPeer::retry already running, target:%s , cur: %s", qPrintable(appId), qPrintable(mId));
		if (isIdEqual(appId))
		{
			return Run_AlreadyRunning;
		}
		else
		{
			return Run_AlreadyRunOtherId;
		}
	}

	if (!mRetryMode)
	{
		qWarning("QtLocalPeer::retry but no retry mode, target: %s", qPrintable(appId));
		return Run_NoRetrymode;
	}
	else {
		QString lockName;
		QString id = appId;
		QString socketName;
		calParameterById(id, socketName, lockName);

		//in fact, this should be not necessary
		mLockFile.close();

		mLockFile.setFileName(lockName);

		bool openres = mLockFile.open(QIODevice::ReadWrite);
		if (!openres)
		{
			//qWarning("QtLocalPeer::retry openFileFail: %s", qPrintable(lockName));
			mLockFile.close();
			return Run_OccupyFail;
		}
		if (!mLockFile.lock(QtLP_Private::QtLockedFile::WriteLock, false))
		{
			mLockFile.close();
			return Run_OccupyFail;
		}

		if (m_pServer->isListening())
		{
			//qWarning("QtLocalPeer::retry m_pServer->isListening()", qPrintable(m_pServer->fullServerName()));
			m_pServer->close();
		}
		bool listenRes = m_pServer->listen(socketName);
#if defined(Q_OS_UNIX) && (QT_VERSION >= QT_VERSION_CHECK(4,5,0))
		// ### Workaround to do
		if (!res && m_pServer->serverError() == QAbstractSocket::AddressInUseError) {
			QFile::remove(QDir::cleanPath(QDir::tempPath()) + QLatin1Char('/') + socketName);
			res = m_pServer->listen(socketName);
		}
#endif
		if (!listenRes)
		{
			qWarning("QtSingleCoreApplication: listen on local socket failed, %s", qPrintable(m_pServer->errorString()));
		}
		QObject::connect(m_pServer, SIGNAL(newConnection()), SLOT(receiveConnection()));//, Qt::UniqueConnection

		mIsClient = false;
		mId = id;
		mSocketName = socketName;
		return Run_Succ;
	}
}
bool QtLocalPeer::sendMessage(const QString &message, int timeout)
{
    if (!mIsClient)
        return false;

    QLocalSocket socket;
    bool connOk = false;
    for(int i = 0; i < 2; i++) {
        // Try twice, in case the other instance is just starting up
        socket.connectToServer(mSocketName);
        connOk = socket.waitForConnected(timeout/2);
        if (connOk || i)
            break;
        int ms = 250;
#if defined(Q_OS_WIN)
        Sleep(DWORD(ms));
#else
        struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
        nanosleep(&ts, NULL);
#endif
    }
    if (!connOk)
        return false;

    QByteArray uMsg(message.toUtf8());
    QDataStream ds(&socket);
    ds.writeBytes(uMsg.constData(), uMsg.size());
    bool res = socket.waitForBytesWritten(timeout);
    if (res) {
        res &= socket.waitForReadyRead(timeout);   // wait for ack
        if (res)
            res &= (socket.read(qstrlen(ack)) == ack);
    }
    return res;
}


void QtLocalPeer::receiveConnection()
{
    QLocalSocket* socket = m_pServer->nextPendingConnection();
    if (!socket)
        return;

    while (true) {
        if (socket->state() == QLocalSocket::UnconnectedState) {
            qWarning("QtLocalPeer: Peer disconnected");
            delete socket;
            return;
        }
        if (socket->bytesAvailable() >= qint64(sizeof(quint32)))
            break;
        socket->waitForReadyRead();
    }

    QDataStream ds(socket);
    QByteArray uMsg;
    quint32 remaining;
    ds >> remaining;
    uMsg.resize(remaining);
    int got = 0;
    char* uMsgBuf = uMsg.data();
    do {
        got = ds.readRawData(uMsgBuf, remaining);
        remaining -= got;
        uMsgBuf += got;
    } while (remaining && got >= 0 && socket->waitForReadyRead(2000));
    if (got < 0) {
        qWarning("QtLocalPeer: Message reception failed %s", socket->errorString().toLatin1().constData());
        delete socket;
        return;
    }
    QString message(QString::fromUtf8(uMsg));
    socket->write(ack, qstrlen(ack));
    socket->waitForBytesWritten(1000);
    socket->waitForDisconnected(1000); // make sure client reads ack
    delete socket;
    emit messageReceived(message); //### (might take a long time to return)
}
