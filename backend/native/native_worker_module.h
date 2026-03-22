#pragma once

#include "native_worker_context.h"

#include <QObject>
#include <QStringList>

class INativeWorkerModule {
public:
    virtual ~INativeWorkerModule() = default;

    virtual QString moduleName() const = 0;
    virtual QStringList supportedJobs() const = 0;
    virtual int run(const NativeWorkerContext& context) = 0;
};

#define INativeWorkerModule_iid "com.ai-frontier.NativeWorkerModule/1.0"
Q_DECLARE_INTERFACE(INativeWorkerModule, INativeWorkerModule_iid)
