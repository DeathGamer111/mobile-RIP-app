#include <QObject>
#include <QString>
#include <QVector>
#include <lcms2.h>


class ColorProfile : public QObject {
    Q_OBJECT

public:
    explicit ColorProfile(QObject *parent = nullptr);
    ~ColorProfile();

    Q_INVOKABLE bool convertToColorspace(const QString &path, const QString &targetSpace);
    Q_INVOKABLE bool loadProfiles(const QString &inputIccPath, const QString &outputIccPath);
    Q_INVOKABLE bool convertWithICCProfiles(const QString &imagePath, const QString &outputPath);
    Q_INVOKABLE bool convertWithICCProfilesCMYK(const QString &imagePath, const QString &outputPath, const QString &inputICCPath, const QString &outputICCPath);
    Q_INVOKABLE bool convertRgbToCmyk(const QString &imagePath);
    Q_INVOKABLE bool convertCmykToRgb(const QString &imagePath);
    Q_INVOKABLE bool convertToGrayscale(const QString &imagePath);
    Q_INVOKABLE bool convertToLcLmLyLk(const QString &imagePath);
    Q_INVOKABLE bool convertToIndexed(const QString &imagePath, bool useAnsi16);


private:
    cmsHPROFILE inputProfile_ = nullptr;
    cmsHPROFILE outputProfile_ = nullptr;

    QVector<QVector<uchar>> getPalette(bool useAnsi16) const;
    int findNearestColorIndex(const QVector<uchar> &rgb, const QVector<QVector<uchar>> &palette) const;
};
