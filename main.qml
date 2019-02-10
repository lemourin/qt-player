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
    preview: true
    //url: "/home/lemourin/Videos/numinous.webm"
    //title: "numinous"
    //url: "http://127.0.0.1:12345/5/eyJpZCI6IntcImlkeFwiOjMsXCJuYW1lXCI6XCJEYXJsaW5nIGluIHRoZSBGcmFuWFggOCBbUExdIFtPcGVubG9hZC5jb10ubXA0XCIsXCJvcmlnaW5cIjpcImh0dHBzOi8vd3d3LmFuaW1lem9uZS5wbC9vZGNpbmVrL2RhcmxpbmctaW4tdGhlLWZyYW54eC84XCIsXCJwbGF5ZXJcIjpcIm9wZW5sb2FkLmNvXCJ9IiwibmFtZSI6IkRhcmxpbmcgaW4gdGhlIEZyYW5YWCA4IFtQTF0gW09wZW5sb2FkLmNvXS5tcDQiLCJzaXplIjozOTg2NjA4NTQsInN0YXRlIjoiNSJ9"
    //title: "franxx"
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
