# RemoteControl

A cross-platform remote device control application built with WebRTC and mediasoup. Control and stream your devices remotely with low-latency video/audio transmission and real-time input injection.

## 🚀 Features

- **Real-time Screen Sharing**: Low-latency video streaming using WebRTC and mediasoup SFU
- **Remote Input Control**: Mouse and keyboard input injection for remote device control
- **Cross-Platform**: Built with Qt6 for Windows, macOS, and Linux support
- **Secure Communication**: WebSocket-based signaling with optional authentication
- **Scalable Architecture**: FastAPI-based signaling server with mediasoup worker processes
- **Optimized Performance**: Hardware-accelerated video encoding and efficient codec negotiation

## 📋 System Requirements

### Client (C++)
- **OS**: Windows 10+, macOS 10.15+, or Linux (Ubuntu 20.04+)
- **Qt**: Qt 6.x or Qt 5.15+
- **Compiler**: C++17 compatible (MSVC, GCC, Clang)
- **Dependencies**: OpenGL, WebSockets support

### Server (Python)
- **Python**: 3.8+
- **Node.js**: 14+ (for mediasoup worker)
- **npm**: 6+ (for node dependencies)

## 🔧 Installation

### Prerequisites

#### On Windows:
```
# Install Qt (via Qt Online Installer)
# https://www.qt.io/download

# Install Node.js and npm
# https://nodejs.org/

# Install Python 3.8+
# https://www.python.org/downloads/
```

#### On macOS:
```
brew install qt@6
brew install node
brew install python@3.9
```

#### On Linux (Ubuntu 20.04+):
```
sudo apt-get install qt6-base-dev qt6-websockets-dev
sudo apt-get install nodejs npm
sudo apt-get install python3 python3-pip
```

### Building the Client

```
cd RemoteControl
cmake -B build
cd build
cmake --build . --config Release
```

### Setting Up the Server

```
# Navigate to project root
cd RemoteControl

# Install Python dependencies
pip install -r requirements.txt

# Install mediasoup worker
npm install mediasoup

# Create .env file (see Configuration section)
cp .env.example .env
```

## ⚙️ Configuration

### Server Configuration (.env)

Create a `.env` file in the project root with the following variables:

```
# Server settings
PORT=5000
HOST=0.0.0.0
LISTEN_IP=0.0.0.0
ANNOUNCED_IP=127.0.0.1

# RTC port range for media transmission
RTC_MIN_PORT=10000
RTC_MAX_PORT=59999

# CORS settings
CORS_ORIGINS=http://localhost,http://localhost:3000,http://localhost:5173

# Optional: Logging level
LOG_LEVEL=DEBUG

# Optional: Session configuration
SESSION_TIMEOUT=3600
```

## 🚀 Running the Application

### Start the Signaling Server

```
# From the RemoteControl directory
python server.py

# Server will start at http://0.0.0.0:5000
```

### Start the Client Application

Run the compiled executable:

```
# On Windows
build/Release/RemoteControl.exe

# On macOS/Linux
./build/RemoteControl
```

## 📖 Project Architecture

```
RemoteControl/
├── core/                    # Core application logic
│   ├── AppController.h/cpp  # Main application controller
│   ├── AppState.h           # Application state management
│   ├── RoomManager.h/cpp    # Room/session management
│   ├── ScreenCapturer.h/cpp # Screen capture and video processing
│   └── ControlEventHandler  # Event handling for remote control
├── mediasoup/               # WebRTC and mediasoup integration
│   ├── MediasoupClient.h/cpp
│   ├── VideoProducer.h/cpp
│   └── VideoDecoder.cpp
├── network/                 # Network and signaling
│   ├── SignalingClient.h/cpp
│   └── WebSocket communication
├── input/                   # Input injection module
│   └── InputInjector.h/cpp  # Mouse/keyboard input handling
├── ui/                      # User interface
│   ├── AppShell.h/cpp       # Main application window
│   ├── ConnectModal.h/cpp   # Connection dialog
│   ├── HostPage.h/cpp       # Host (server) view
│   └── ViewerPage.h/cpp     # Viewer (client) view
├── mediasoup-prebuilt/      # Pre-built mediasoup includes
├── server.py                # FastAPI signaling server
├── CMakeLists.txt           # CMake build configuration
└── requirements.txt         # Python dependencies
```

## 🔌 API Endpoints

### WebSocket Events

**Client → Server:**
- `connect_room`: Join a control session
- `disconnect_room`: Leave a control session
- `remote_mouse_event`: Send mouse coordinates/events
- `remote_keyboard_event`: Send keyboard events
- `request_video`: Request video stream parameters

**Server → Client:**
- `room_joined`: Acknowledgment of room join
- `peer_connected`: Notification of new peer connection
- `video_params`: Video codec and RTP parameters
- `control_ack`: Acknowledgment of control events

### RTC Data Channels

- **video**: H.264/VP8 encoded video stream
- **control**: Bi-directional control command exchange

## 🔐 Security Considerations

- Use HTTPS/WSS in production environments
- Implement authentication at the signaling server level
- Validate all incoming control events
- Use firewall rules to restrict RTC port ranges
- Enable CORS only for trusted origins

## 📊 Performance Optimization

### Video Encoding
- Adaptive bitrate based on network conditions
- Hardware acceleration support (NVENC, QuickSync)
- Multiple codec support (H.264, VP8, VP9)

### Network
- Port reuse for efficient NAT traversal
- Bandwidth management and congestion control
- Automatic reconnection on network failure

## 🐛 Troubleshooting

### Common Issues

**"Node.js not found" error:**
```
# Ensure Node.js is in PATH
which node  # or 'where node' on Windows

# If not found, add Node.js to PATH
export PATH=/path/to/nodejs:$PATH
```

**WebSocket connection fails:**
- Check firewall rules for port 5000
- Verify CORS_ORIGINS in .env matches client origin
- Check server logs: `LOG_LEVEL=DEBUG`

**Video streaming issues:**
- Verify RTC ports (10000-59999) are accessible
- Check network bandwidth availability
- Monitor CPU/GPU usage during streaming

## 📝 Development

### Building in Debug Mode

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cd build
cmake --build . --config Debug
```

### Running Tests

```
cd build
ctest
```

## 📦 Dependencies

### C++ Libraries
- Qt 6.x/5.15+ (UI, WebSockets, Networking)
- mediasoup-client (WebRTC)
- OpenGL (Video rendering)

### Python Libraries
- fastapi (Web framework)
- python-socketio (WebSocket support)
- uvicorn (ASGI server)
- python-dotenv (Environment configuration)

See `requirements.txt` for complete Python dependency list.

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🤝 Contributing

Contributions are welcome! Please follow these guidelines:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📧 Support

For issues, questions, or suggestions, please [open an issue](https://github.com/sushrut007/RemoteControl/issues) on GitHub.

## 🙏 Acknowledgments

- [mediasoup](https://mediasoup.org/) - SFU (Selective Forwarding Unit)
- [FastAPI](https://fastapi.tiangolo.com/) - Modern Python web framework
- [Qt](https://www.qt.io/) - Cross-platform application development framework


---