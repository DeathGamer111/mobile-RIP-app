#include "ImageImportManager.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#if defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace {
constexpr int RequestPickImage = 4201;
constexpr int RequestCaptureImage = 4202;
constexpr int RequestImportImage = 4203;
constexpr int AndroidResultOk = -1;
constexpr int AndroidFlagGrantReadUriPermission = 1;

QString importDirectory()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        return {};

    QDir dir(root);
    if (!dir.mkpath(QStringLiteral("incoming_images")))
        return {};

    return dir.filePath(QStringLiteral("incoming_images"));
}
}

ImageImportManager::ImageImportManager(QObject *parent)
    : QObject(parent)
{
}

bool ImageImportManager::supportsNativeImagePicker() const
{
#if defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}

bool ImageImportManager::supportsCamera() const
{
#if defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}

void ImageImportManager::openImagePicker()
{
#if defined(Q_OS_ANDROID)
    QAndroidIntent intent(QStringLiteral("android.intent.action.OPEN_DOCUMENT"));
    QJniObject handle = intent.handle();
    handle.callObjectMethod("addCategory",
                            "(Ljava/lang/String;)Landroid/content/Intent;",
                            QJniObject::fromString(QStringLiteral("android.intent.category.OPENABLE")).object<jstring>());
    handle.callObjectMethod("setType",
                            "(Ljava/lang/String;)Landroid/content/Intent;",
                            QJniObject::fromString(QStringLiteral("image/*")).object<jstring>());
    handle.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(AndroidFlagGrantReadUriPermission));
    QtAndroidPrivate::startActivity(handle, RequestPickImage, this);
#else
    emit failed(QStringLiteral("Use the file picker to select an image on this platform."));
#endif
}

void ImageImportManager::openImageImportChooser()
{
#if defined(Q_OS_ANDROID)
    QAndroidIntent pickIntent(QStringLiteral("android.intent.action.OPEN_DOCUMENT"));
    QJniObject pickHandle = pickIntent.handle();
    pickHandle.callObjectMethod("addCategory",
                                "(Ljava/lang/String;)Landroid/content/Intent;",
                                QJniObject::fromString(QStringLiteral("android.intent.category.OPENABLE")).object<jstring>());
    pickHandle.callObjectMethod("setType",
                                "(Ljava/lang/String;)Landroid/content/Intent;",
                                QJniObject::fromString(QStringLiteral("image/*")).object<jstring>());
    pickHandle.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(AndroidFlagGrantReadUriPermission));

    QAndroidIntent cameraIntent(QStringLiteral("android.media.action.IMAGE_CAPTURE"));

    QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent",
        "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        pickHandle.object<jobject>(),
        QJniObject::fromString(QStringLiteral("Upload Image")).object<jstring>());

    QJniEnvironment env;
    jclass intentClass = env->FindClass("android/content/Intent");
    jobjectArray initialIntents = env->NewObjectArray(1, intentClass, cameraIntent.handle().object<jobject>());
    if (initialIntents) {
        QJniObject extraInitialIntents = QJniObject::getStaticObjectField(
            "android/content/Intent",
            "EXTRA_INITIAL_INTENTS",
            "Ljava/lang/String;");
        chooser.callObjectMethod("putExtra",
                                 "(Ljava/lang/String;[Landroid/os/Parcelable;)Landroid/content/Intent;",
                                 extraInitialIntents.object<jstring>(),
                                 initialIntents);
        env->DeleteLocalRef(initialIntents);
    }
    env->DeleteLocalRef(intentClass);

    QtAndroidPrivate::startActivity(chooser, RequestImportImage, this);
#else
    emit failed(QStringLiteral("Use the file picker to select an image on this platform."));
#endif
}

void ImageImportManager::openCamera()
{
#if defined(Q_OS_ANDROID)
    QAndroidIntent intent(QStringLiteral("android.media.action.IMAGE_CAPTURE"));
    QtAndroidPrivate::startActivity(intent.handle(), RequestCaptureImage, this);
#else
    emit failed(QStringLiteral("Camera capture is not available on this platform."));
#endif
}

#if defined(Q_OS_ANDROID)
void ImageImportManager::handleActivityResult(int receiverRequestCode, int resultCode, const QJniObject &data)
{
    if (receiverRequestCode != RequestPickImage
            && receiverRequestCode != RequestCaptureImage
            && receiverRequestCode != RequestImportImage)
        return;

    if (resultCode != AndroidResultOk) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { emit canceled(); },
                                  Qt::QueuedConnection);
        return;
    }

    if (!data.isValid()) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            emit failed(QStringLiteral("No image was returned."));
        }, Qt::QueuedConnection);
        return;
    }

    const QJniObject uri = data.callObjectMethod("getData", "()Landroid/net/Uri;");
    if (receiverRequestCode == RequestPickImage || uri.isValid()) {
        const QString path = copyUriToLocalFile(uri, QStringLiteral("jpg"));
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, path]() {
            if (path.isEmpty())
                emit failed(QStringLiteral("Could not import the selected image."));
            else
                emit imageReady(QUrl::fromLocalFile(path).toString());
        }, Qt::QueuedConnection);
        return;
    }

    const QJniObject extras = data.callObjectMethod("getExtras", "()Landroid/os/Bundle;");
    if (!extras.isValid()) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            emit failed(QStringLiteral("Camera did not return an image."));
        }, Qt::QueuedConnection);
        return;
    }

    const QJniObject bitmap = extras.callObjectMethod("get",
                                                     "(Ljava/lang/String;)Ljava/lang/Object;",
                                                     QJniObject::fromString(QStringLiteral("data")).object<jstring>());
    const QString path = saveCameraBitmap(bitmap);
    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, path]() {
        if (path.isEmpty())
            emit failed(QStringLiteral("Camera image could not be saved."));
        else
            emit imageReady(QUrl::fromLocalFile(path).toString());
    }, Qt::QueuedConnection);
}

QString ImageImportManager::copyUriToLocalFile(const QJniObject &uri, const QString &fallbackExtension)
{
    if (!uri.isValid())
        return {};

    QJniObject context(QtAndroidPrivate::context());
    QJniObject resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    if (!resolver.isValid())
        return {};

    QString extension = fallbackExtension;
    QJniObject mime = resolver.callObjectMethod("getType",
                                                "(Landroid/net/Uri;)Ljava/lang/String;",
                                                uri.object<jobject>());
    const QString mimeString = mime.toString();
    if (mimeString == QStringLiteral("image/png"))
        extension = QStringLiteral("png");
    else if (mimeString == QStringLiteral("image/webp"))
        extension = QStringLiteral("webp");
    else if (mimeString == QStringLiteral("image/bmp"))
        extension = QStringLiteral("bmp");
    else if (mimeString == QStringLiteral("image/tiff")
             || mimeString == QStringLiteral("image/x-tiff"))
        extension = QStringLiteral("tif");

    const QString outputPath = newImportPath(extension, displayNameForUri(uri));
    if (outputPath.isEmpty())
        return {};

    QJniObject input = resolver.callObjectMethod("openInputStream",
                                                "(Landroid/net/Uri;)Ljava/io/InputStream;",
                                                uri.object<jobject>());
    if (!input.isValid())
        return {};

    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly))
        return {};

    QJniEnvironment env;
    jbyteArray buffer = env->NewByteArray(16 * 1024);
    if (!buffer)
        return {};

    QByteArray chunk;
    chunk.resize(16 * 1024);

    while (true) {
        const jint bytesRead = input.callMethod<jint>("read", "([B)I", buffer);
        if (bytesRead <= 0)
            break;
        env->GetByteArrayRegion(buffer, 0, bytesRead, reinterpret_cast<jbyte *>(chunk.data()));
        output.write(chunk.constData(), bytesRead);
    }

    input.callMethod<void>("close", "()V");
    env->DeleteLocalRef(buffer);
    output.close();

    return output.size() > 0 ? outputPath : QString();
}

QString ImageImportManager::displayNameForUri(const QJniObject &uri) const
{
    if (!uri.isValid())
        return {};

    QJniObject context(QtAndroidPrivate::context());
    QJniObject resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    if (!resolver.isValid())
        return {};

    QJniObject displayNameColumn = QJniObject::getStaticObjectField(
        "android/provider/OpenableColumns",
        "DISPLAY_NAME",
        "Ljava/lang/String;");

    QJniEnvironment env;
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray projection = env->NewObjectArray(1, stringClass, displayNameColumn.object<jstring>());
    QJniObject cursor = resolver.callObjectMethod(
        "query",
        "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
        uri.object<jobject>(),
        projection,
        nullptr,
        nullptr,
        nullptr);
    env->DeleteLocalRef(projection);
    env->DeleteLocalRef(stringClass);

    if (!cursor.isValid())
        return {};

    QString name;
    if (cursor.callMethod<jboolean>("moveToFirst", "()Z")) {
        const jint index = cursor.callMethod<jint>(
            "getColumnIndex",
            "(Ljava/lang/String;)I",
            displayNameColumn.object<jstring>());
        if (index >= 0)
            name = cursor.callObjectMethod("getString", "(I)Ljava/lang/String;", index).toString();
    }
    cursor.callMethod<void>("close", "()V");
    return name;
}

QString ImageImportManager::saveCameraBitmap(const QJniObject &bitmap)
{
    if (!bitmap.isValid())
        return {};

    const QString outputPath = newImportPath(QStringLiteral("png"), QStringLiteral("Camera Image"));
    if (outputPath.isEmpty())
        return {};

    QJniObject stream("java/io/ByteArrayOutputStream");
    QJniObject format = QJniObject::getStaticObjectField("android/graphics/Bitmap$CompressFormat",
                                                         "PNG",
                                                         "Landroid/graphics/Bitmap$CompressFormat;");
    const bool compressed = bitmap.callMethod<jboolean>("compress",
                                                        "(Landroid/graphics/Bitmap$CompressFormat;ILjava/io/OutputStream;)Z",
                                                        format.object<jobject>(),
                                                        jint(95),
                                                        stream.object<jobject>());
    if (!compressed)
        return {};

    QJniObject byteArray = stream.callObjectMethod("toByteArray", "()[B");
    QJniEnvironment env;
    jbyteArray array = byteArray.object<jbyteArray>();
    const jsize size = env->GetArrayLength(array);
    QByteArray bytes;
    bytes.resize(size);
    env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte *>(bytes.data()));

    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly))
        return {};
    output.write(bytes);
    output.close();
    return outputPath;
}

QString ImageImportManager::newImportPath(const QString &extension, const QString &baseName) const
{
    const QString dir = importDirectory();
    if (dir.isEmpty())
        return {};

    QString safeBase = QFileInfo(baseName).completeBaseName();
    safeBase.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
    if (safeBase.isEmpty())
        safeBase = QStringLiteral("incoming");

    return QDir(dir).filePath(QStringLiteral("%1_%2.%3")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(safeBase)
        .arg(extension.isEmpty() ? QStringLiteral("jpg") : extension));
}
#endif
