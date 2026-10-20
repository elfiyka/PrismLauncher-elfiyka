#include "AuthServer.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QUrl>
#include "Application.h"
#include "FileSystem.h"
#include "launch/LaunchStep.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/auth/AuthSession.h"
#include "net/Download.h"
#include "net/NetJob.h"

namespace {
static QHash<const MinecraftInstance*, std::shared_ptr<QString>> s_injectors;
static QMutex s_mutex;

class InjectAuthlib : public LaunchStep {
    std::shared_ptr<QString> m_arg;
    bool m_offline;
    shared_qobject_ptr<NetJob> m_job;

    void finish(const QString& jar) {
        if (!isRunning())
            return;
        if (m_arg) {
            quint16 port = AuthServer::instance() ? AuthServer::instance()->port() : 0;
            QString argStr = QString("-javaagent:%1=http://localhost:%2/auth/")
                                 .arg(QDir::toNativeSeparators(jar))
                                 .arg(port);
            QMutexLocker lock(&s_mutex);
            *m_arg = std::move(argStr);
        }
        emitSucceeded();
    }

   public:
    InjectAuthlib(LaunchTask* parent, std::shared_ptr<QString> arg, bool offline)
        : LaunchStep(parent), m_arg(std::move(arg)), m_offline(offline) {}

    bool canAbort() const override { return !m_job || m_job->canAbort(); }
    bool abort() override {
        if (m_job && m_job->canAbort())
            m_job->abort();
        emitAborted();
        return true;
    }

    void executeTask() override {
        QDir dir("injectors");
        dir.mkpath(".");
        auto jars = dir.entryInfoList({ "*.jar" }, QDir::Files, QDir::Time);
        for (const auto& j : jars) {
            if (j.size() > 0) {
                finish(j.absoluteFilePath());
                return;
            }
        }
        if (m_offline) {
            emitFailed(tr("Offline mode: no authlib-injector found in injectors/."));
            return;
        }

        auto job = new NetJob("authlib-injector", APPLICATION->network());
        QString ver = dir.absoluteFilePath("version.json");
        job->addNetAction(Net::Download::makeFile(QUrl("https://authlib-injector.yushi.moe/artifact/latest.json"), ver));
        m_job.reset(job);
        connect(job, &NetJob::failed, this, [this](QString r) {
            if (isRunning())
                emitFailed(r);
        });
        connect(job, &NetJob::succeeded, this, [this, ver, dir]() {
            m_job.reset();
            if (!isRunning())
                return;
            QString url;
            try {
                url = QJsonDocument::fromJson(FS::read(ver)).object().value("download_url").toString();
            } catch (...) {}
            if (url.isEmpty()) {
                if (isRunning())
                    emitFailed(tr("Failed to parse injector URL."));
                return;
            }

            QString jar = dir.absoluteFilePath(QFileInfo(url).fileName());
            if (QFileInfo::exists(jar) && QFileInfo(jar).size() > 0) {
                finish(jar);
                return;
            }

            auto dl = new NetJob("authlib-injector dl", APPLICATION->network());
            dl->addNetAction(Net::Download::makeFile(QUrl(url), jar));
            m_job.reset(dl);
            connect(dl, &NetJob::failed, this, [this](QString r) {
                if (isRunning())
                    emitFailed(r);
            });
            connect(dl, &NetJob::succeeded, this, [this, jar]() { finish(jar); });
            dl->start();
        });
        job->start();
    }
};
}  // namespace

namespace Elfiyka {
void injectLaunch(MinecraftInstance* instance, LaunchTask* task, AuthSession* session) {
    if (!instance || !task)
        return;
    auto arg = std::make_shared<QString>();
    {
        QMutexLocker lock(&s_mutex);
        if (!s_injectors.contains(instance)) {
            QObject::connect(instance, &QObject::destroyed, [instance]() {
                QMutexLocker lock(&s_mutex);
                s_injectors.remove(instance);
            });
        }
        s_injectors[instance] = arg;
    }
    if (session && !session->uuid.isEmpty() && !session->player_name.isEmpty()) {
        AuthServer::registerUser(session->uuid, session->player_name);
    }
    task->appendStep(makeShared<InjectAuthlib>(task, arg, session && session->launchMode == LaunchMode::Offline));
}

void injectJavaArgs(const MinecraftInstance* instance, QStringList& args) {
    QMutexLocker lock(&s_mutex);
    auto arg = s_injectors.value(instance);
    if (arg && !arg->isEmpty()) {
        args.append(*arg);
    }
}
}  // namespace Elfiyka
