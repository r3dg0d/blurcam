//@ pragma Env QT_QUICK_CONTROLS_STYLE=Fusion
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Quickshell
import Quickshell.Io

ShellRoot {
    id: shell

    readonly property string executable: Quickshell.env("BLURCAM_BIN") || "blurcam"
    property string activity: ""
    property string status: "Ready"
    property string output: ""
    property bool stopping: false
    property bool closeAfterExit: false
    readonly property bool busy: activity !== ""

    function append(line) {
        output = (output + line + "\n").slice(-16000)
    }

    function start(label, args) {
        if (busy)
            return
        output = ""
        status = "Starting " + label.toLowerCase() + "…"
        activity = label
        worker.command = [executable].concat(args)
        worker.running = true
        launchTimeout.restart()
    }

    function stop() {
        if (!busy || stopping)
            return
        stopping = true
        status = "Stopping…"
        worker.running = false
    }

    function finish(message) {
        launchTimeout.stop()
        status = message
        activity = ""
        stopping = false
        if (closeAfterExit)
            Qt.quit()
    }

    function localPath(url) {
        const value = url.toString()
        return value.startsWith("file:///") ? decodeURIComponent(value.slice(7)) : ""
    }

    function run() {
        const settings = [
            "--effect", effect.currentValue, "--faces", "all", "--debug=false",
            "--privacy-failsafe=" + (failsafe.checked ? "true" : "false"),
            "--failsafe-mode", protection.currentValue
        ]
        if (modes.currentIndex === 2) {
            start("File export", ["file", "--overwrite=" + (overwrite.checked ? "true" : "false"),
                                  "--output=" + destination.text, ...settings, "--", source.text])
        } else if (modes.currentIndex === 1) {
            start("Virtual camera", ["virtualcam", "--device=" + camera.text,
                                     "--output=" + loopback.text].concat(settings))
        } else {
            start("Camera preview", ["preview", "--device=" + camera.text].concat(settings))
        }
    }

    Process {
        id: worker
        stdout: SplitParser { onRead: line => shell.append(line) }
        stderr: SplitParser { onRead: line => shell.append(line) }
        onStarted: {
            launchTimeout.stop()
            if (!shell.stopping)
                shell.status = shell.activity + " running"
        }
        onExited: (code, exitStatus) => {
            if (shell.stopping)
                shell.finish("Stopped")
            else if (code === 0 && exitStatus === 0)
                shell.finish(shell.activity + " completed")
            else
                shell.finish(shell.activity + " failed (exit " + code + ")")
        }
    }

    Timer {
        id: launchTimeout
        interval: 3000
        onTriggered: {
            if (worker.processId)
                return
            worker.running = false
            shell.append("Could not launch " + shell.executable + ". Check PATH or BLURCAM_BIN.")
            shell.finish("Could not start BlurCam")
        }
    }

    ApplicationWindow {
        id: window
        visible: true
        title: "BlurCam"
        width: 760
        height: 650
        minimumWidth: 620
        minimumHeight: 610
        color: "#10151e"
        palette.window: "#10151e"
        palette.base: "#131c29"
        palette.button: "#253449"
        palette.windowText: "#e9f1ff"
        palette.text: "#e9f1ff"
        palette.buttonText: "#e9f1ff"
        palette.placeholderText: "#a4b2c7"
        palette.highlight: "#7cead0"
        palette.highlightedText: "#10151e"

        onClosing: event => {
            event.accepted = false
            if (shell.busy) {
                shell.closeAfterExit = true
                shell.stop()
            } else {
                Qt.quit()
            }
        }

        FileDialog {
            id: inputDialog
            title: "Choose input video"
            onAccepted: source.text = shell.localPath(selectedFile)
        }

        FileDialog {
            id: outputDialog
            title: "Choose output video"
            fileMode: FileDialog.SaveFile
            defaultSuffix: "mp4"
            nameFilters: ["Video files (*.mp4 *.mkv *.webm)"]
            onAccepted: destination.text = shell.localPath(selectedFile)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 2
                    Label { text: "BlurCam"; font.pixelSize: 30; font.bold: true }
                    Label { text: "Local face anonymization"; color: "#a4b2c7" }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Install face model"
                    enabled: !shell.busy
                    onClicked: shell.start("Model installation", ["models", "install"])
                }
            }

            TabBar {
                id: modes
                Layout.fillWidth: true
                enabled: !shell.busy
                TabButton { text: "Camera preview" }
                TabButton { text: "Virtual camera" }
                TabButton { text: "Video file" }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 12
                rowSpacing: 8
                enabled: !shell.busy
                readonly property bool fileMode: modes.currentIndex === 2
                readonly property bool virtualMode: modes.currentIndex === 1
                id: form

                Label { text: "Camera"; visible: !form.fileMode }
                TextField {
                    id: camera
                    text: "/dev/video0"
                    visible: !form.fileMode
                    Layout.fillWidth: true
                    Accessible.name: "Camera device or friendly name"
                }
                Button {
                    text: "List devices"
                    visible: !form.fileMode
                    onClicked: shell.start("Device scan", ["devices", "--short"])
                }

                Label { text: "Loopback output"; visible: form.virtualMode }
                TextField {
                    id: loopback
                    text: "/dev/video10"
                    visible: form.virtualMode
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    Accessible.name: "Loopback output device"
                }

                Label { text: "Input video"; visible: form.fileMode }
                TextField {
                    id: source
                    placeholderText: "/path/to/source.mp4"
                    visible: form.fileMode
                    Layout.fillWidth: true
                    Accessible.name: "Input video"
                }
                Button { text: "Browse…"; visible: form.fileMode; onClicked: inputDialog.open() }

                Label { text: "Output video"; visible: form.fileMode }
                TextField {
                    id: destination
                    placeholderText: "/path/to/filtered.mp4"
                    visible: form.fileMode
                    Layout.fillWidth: true
                    Accessible.name: "Output video"
                }
                Button { text: "Save as…"; visible: form.fileMode; onClicked: outputDialog.open() }

                Item { visible: form.fileMode }
                CheckBox {
                    id: overwrite
                    text: "Replace existing output"
                    visible: form.fileMode
                    Layout.columnSpan: 2
                }

                Label { text: "Face effect" }
                ComboBox {
                    id: effect
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    Accessible.name: "Face effect"
                    textRole: "label"
                    valueRole: "value"
                    model: [
                        { label: "Blur", value: "blur" },
                        { label: "Pixelate", value: "pixelate" },
                        { label: "Opaque censor", value: "censor" },
                        { label: "Glitch", value: "glitch" }
                    ]
                }

                CheckBox {
                    id: failsafe
                    text: "Privacy fail-safe"
                    checked: true
                    Layout.columnSpan: 2
                }
                ComboBox {
                    id: protection
                    enabled: failsafe.checked
                    Accessible.name: "Fail-safe coverage"
                    textRole: "label"
                    valueRole: "value"
                    model: [
                        { label: "Full frame", value: "full-frame" },
                        { label: "Upper body", value: "upper-body" },
                        { label: "Last known region", value: "last-known-region" },
                        { label: "Expanded face", value: "expanded-face" }
                    ]
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#a4b2c7"
                text: form.fileMode ? "Exports MP4, MKV or WebM. Compatible audio tracks are copied."
                    : form.virtualMode ? "Publish filtered video to an existing v4l2loopback device."
                    : "Opens the filtered camera feed in a separate preview window."
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: shell.status
                    color: "#7cead0"
                    font.bold: true
                    Layout.fillWidth: true
                }
                Button {
                    text: "Stop"
                    enabled: shell.busy && !shell.stopping
                    onClicked: shell.stop()
                }
                Button {
                    text: form.fileMode ? "Export video" : form.virtualMode ? "Start virtual camera" : "Open preview"
                    highlighted: true
                    enabled: !shell.busy && (form.fileMode
                        ? source.text.startsWith("/") && destination.text.startsWith("/")
                            && /\.(mp4|mkv|webm)$/i.test(destination.text) && source.text !== destination.text
                        : camera.text.trim() !== "" && (!form.virtualMode
                            || /^\/dev\/video[0-9]+$/.test(loopback.text) && camera.text !== loopback.text))
                    onClicked: shell.run()
                }
            }

            Label { text: "Activity log"; font.bold: true }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 100
                clip: true
                TextArea {
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: shell.output
                    placeholderText: "BlurCam messages will appear here."
                    onTextChanged: cursorPosition = length
                }
            }
        }
    }
}
