#pragma once

#include <QString>
#include <QStringList>

class AssetManager
{
public:
    AssetManager() = default;

    bool initialize(const QString& subdir = "runtime_assets");
    QString rootPath() const;
    QString assetPath(const QString& fileName) const;
    bool hasAsset(const QString& fileName) const;

    bool copyResourceIfMissing(const QString& resourcePath, const QString& fileName);
    bool copyResourcesIfMissing(const QStringList& resourcePaths, const QStringList& fileNames);
    bool syncResource(const QString& resourcePath, const QString& fileName);
    bool syncResources(const QStringList& resourcePaths, const QStringList& fileNames);

    bool cleanup();

private:
    QString m_rootPath;
};
