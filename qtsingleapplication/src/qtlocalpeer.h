// Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
// SPDX-License-Identifier: BSD-3-Clause

#ifndef QTLOCALPEER_H
#define QTLOCALPEER_H

#include <QLocalServer>
#include <QLocalSocket>
#include <QDir>

#include "qtlockedfile.h"

class QtLocalPeer : public QObject
{
    Q_OBJECT
public:
	enum RunRes
	{
		Run_NoRetrymode = -3,
		Run_AlreadyRunning = -2,
		Run_AlreadyRunOtherId = -1,
		Run_Succ = 0,
		Run_OccupyFail = 1,
	};
public:
    QtLocalPeer(QObject *parent = 0, const QString &appId = QString(), const bool retryMode = false);
	~QtLocalPeer();
	bool tryRunMaster();
	bool isRunClient() const { return mIsClient; };
	RunRes retry(const QString &appId);
    bool sendMessage(const QString &message, int timeout);
    QString applicationId() const
        { return mId; }
	bool isIdEqual(const QString& id) const;
Q_SIGNALS:
    void messageReceived(const QString &message);

protected Q_SLOTS:
    void receiveConnection();
private:
	static QString adjustIdAndGenPrefix(QString& srcId);
	static void calParameterById(QString& srcId, QString& socketName, QString& lockfName);
protected:
	bool mIsClient = true;
    QString mId;
    QString mSocketName;
    QLocalServer* m_pServer = nullptr;
    QtLP_Private::QtLockedFile mLockFile;
private:
	const bool mRetryMode = false;
private:
    static const char* ack;
};

#endif // QTLOCALPEER_H
