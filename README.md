# RIP Application

A cross-platform (Linux & Android) Qt-based Raster Image Processing (RIP) application designed for production printing environments. RIP App enables flexible print job management, image editing, ICC color profile conversion, and PRN output generation for both CUPS-supported printers and Nocai hardware.

---

## 🔧 Features

- **Print Job Management**: Create, edit, and persist multiple print jobs with JSON serialization.
- **Image Editing**: Crop, rotate, flip, adjust brightness/contrast, and apply color conversions using ImageMagick.
- **Color Profile Transformation**: Convert color spaces using LittleCMS (lcms2) with support for ICC profiles.
- **Stochastic FM Screening**: Apply high-quality halftoning using custom blue noise masks.
- **Dot Compensation**: Classify and promote ink dot sizes based on pixel neighborhoods for 2BPP Nocai output.
- **PRN Output**: Export to CUPS printers or generate proprietary 2BPP PRN files for Nocai printers.

---

## 🖼 Screenshots & Workflow

A flowchart describing the application’s workflow is available in the repository under `docs/` or as part of the Software Design Document.

---

## 📂 Project Structure

### Backend Components (C++ / Qt)
- `PrintJobModel`: Manages all print job data
- `PrintJobOutput`: Handles network printing via CUPS
- `PrintJobNocai`: Custom backend for 2BPP output and blue noise dithering
- `ImageEditor`: Core image editing interface using ImageMagick (Magick++)
- `MetadataInspector`: Metadata extraction from images, PDFs, and SVGs

### Frontend (QML)
- `Main.qml`, `JobListView.qml`, `JobDetailsView.qml`, `ImageEditorView.qml`, `PrinterSetupView.qml`, `ImpositionView.qml`

---

## 📦 Build & Deploy

### Linux
- Requires: Qt 6, CUPS, ImageMagick, lcms2, poppler, librsvg
- Deployable via AppImage or `.deb`

### Android
- Bundled with static builds of all dependencies
- Packaged using Qt for Android

Build system: `qmake` or `CMake`

---

## 🔒 Security

- All processing is done locally (no cloud or server dependencies)
- No personal user data is stored
- Temporary files are purged automatically unless saved
- Optional checksum validation for print jobs and images

---

## 📈 Performance

- Startup time < 2 seconds on modern hardware
- Handles moderate-size raster jobs with optimized CPU-bound processing
- Output generation is memory-efficient and modular

---

## 🧪 Testing

- Manual testing across supported file formats
- Visual confirmation of edits and PRN output
- ICC profile accuracy verified with sample jobs

---

## 📄 Documentation

This repository includes:
- 📘 **[RIP-App Software Design Document](docs/RIP-App_Software_Design_Document.pdf)** – technical and architectural overview
- 🧭 **[RIP-App Workflow Flowchart](docs/RIP-App_Workflow_Flowchart.pdf)** – visual overview of application operation

---

## 🔗 License

Licensed under [MIT](LICENSE).

---

## 🙋‍♂️ Author

Created and maintained by **Austin McCall**.
