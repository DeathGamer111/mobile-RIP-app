// PrintJob.h
#include <QString>
#include <QPoint>
#include <QDateTime>
#include <QSize>

/*******************************************************************
    PrintJob struct encapsulates all relevant data for a print task.
    Used as a model for job management, configuration, and output.
********************************************************************/

struct PrintJob {
    QString id;                 // Unique identifier for the job
    QString name;               // Display name
    QString imagePath;          // Path to the input image

    QSize paperSize;            // Paper size in pixels (or user-defined)
    double mediaHeightMm = -1.0; // Optional, in 0.1 mm increments; -1 keeps current height
    QSize resolution;           // Output resolution (DPI)
    QPoint offset;              // Position offset on page

    QString whiteStrategy;      // Strategy for white ink printing
    QString varnishType;        // Type of varnish applied
    QString colorProfile;       // ICC color profile or label

    QDateTime createdAt;        // Timestamp when job was created
    
    QString whitePlatePath;	// optional grayscale raster plate for White Plate mode
    QString varnishPlatePath;	// optional grayscale raster plate for Varnish Plate mode
};
