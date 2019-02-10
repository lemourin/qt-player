import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Window 2.2

ApplicationWindow {
  id: root
  visible: true
  width: 640
  height: 360
  title: qsTr("MPV")

  MediaPlayer {
    id: player
    anchors.fill: parent
    focus: true
    player: "mpv"
    type: "video"
    url: "/home/lemourin/Videos/haruhi.mkv"
    title: "haruhi.mkv"
    volume: 1
    onEnded: title = "ended"
    onFullscreenChanged: {
      if (fullscreen) {
        root.showFullScreen();
      } else {
        root.show();
      }
    }
  }
}
