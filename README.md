# RemoteControl (Darpan) - P2P WebRTC Remote Desktop

A high-performance, cross-platform remote desktop and device control application built with **Peer-to-Peer (P2P) WebRTC** via **`libdatachannel`** (the same architecture used in **CrossDesk**).

Video frames and control events flow **directly between peers** with ultra-low latency, while a lightweight FastAPI signaling server connects devices on local LANs and across the Internet.

---

## 🚀 Key Features

- **True Peer-to-Peer (P2P)**: Direct media and data channel transmission without routing heavy screen video through a server.
- **NAT Traversal**: Built-in ICE with STUN support (and optional TURN relay fallback) to connect seamlessly across same and cross-network environments.
- **Hardware-Accelerated Screen Capture**: Native Windows DXGI 1.2 Desktop Duplication API with fallback to GDI.
- **Hardware-Accelerated Video Encoding**: Media Foundation H.264 MFT encoder streaming directly over WebRTC video tracks (`rtc::Track`).
- **Low-Latency Input Control**: Mouse and keyboard injection via Windows `SendInput` transported over raw WebRTC SCTP DataChannels (`rtc::DataChannel`).
- **Lean Architecture**: Replaced the previous 15+ mediasoup library dependencies with a single, high-performance `libdatachannel` engine.
- **Modern UI**: Polished Qt6 desktop interface with customizable connection settings, role selection, and live diagnostics.

---

## 📋 System Requirements

### Client (C++)
- **OS**: Windows 10/11 (x64)
- **Compiler**: MSVC 2019/2022 (C++17)
- **Qt**: Qt 6.5+ (Core, Gui, Widgets, Svg, WebSockets, Network, OpenGLWidgets)
- **WebRTC**: `libdatachannel` (prebuilt included in `libdatachannel-prebuilt/`)

### Signaling Server (Python)
- **Python**: 3.8+
- **Dependencies**: `fastapi`, `python-socketio`, `uvicorn`, `python-dotenv`
*(No Node.js or mediasoup workers required!)*

---

## 🔧 Installation & Build

### 1. Starting the Signaling Server

```powershell
# Install Python dependencies
pip install fastapi python-socketio uvicorn python-dotenv

# Run signaling server
python server.py
# Server starts at http://0.0.0.0:5000
```

### 2. Building the C++ Client

Using CMake presets with Visual Studio MSVC and Ninja:

```powershell
# Configure preset
cmake --preset Release-x64

# Build executable
cmake --build out/build/release --config Release
```

The compiled binary `Darpan.exe` will be located in `out/build/release/`.

---

## 📖 Architecture Overview

```
[ Signaling Server (FastAPI + Socket.IO) ]
           /                       \
  (Exchange Room, SDP Offer/Answer, ICE Candidates)
         /                           \
        v                             v
[ Host Client ] <=================> [ Viewer Client ]
                  Direct P2P WebRTC:
                  - Video: H.264 RTP Stream (SRTP)
                  - Input: SCTP DataChannel
```
