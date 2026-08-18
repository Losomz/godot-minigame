
import './godot-sdk'
import './godot'
const exe = '/engine/godot';
const pack = '/engine/demo-pck.bin';
GODOTSDK.load_pack1 = function(path){
  let fs = wx.getFileSystemManager()
  let file = fs.readFileSync(path, undefined, 0)
  console.log('====load_pack1====',path, file)
  let me = GODOTSDK.engine
  me.rtenv['copyToFS'](file.path, file.buffer);
}

GODOTSDK.startGame(exe, pack)