#pragma once

#include <QtCore/qglobal.h>

#if defined(PRINTFLOW_PRINTER_API_LIBRARY)
#  define PRINTFLOW_PRINTER_API Q_DECL_EXPORT
#else
#  define PRINTFLOW_PRINTER_API Q_DECL_IMPORT
#endif
