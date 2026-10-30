// Writes the .DS_Store that makes Finder open the FAF Native disk image as designed: a window of a
// set size with no toolbar or sidebar, the background picture (.background/background.tiff), large
// icons and each item where the picture expects it. Finder reads the file when it opens the
// volume. Nothing asks Finder to arrange the window (no AppleScript, so no Automation permission
// and no logged-in session needed). It leaves the app itself alone: even the flag that hides
// ".app" is Finder information on the bundle, which a strict signature check rejects, and Finder
// hides the extension of an app anyway.
//
// The file is a "buddy allocator" holding one B-tree of records, each a file name, a four-letter
// code and a typed value; Finder's own files show the layout, and dmgbuild's ds_store and mac_alias
// Python modules write the same records. Finder finds the background through both an alias record
// (in the icon view settings) and a bookmark.
//
// Usage: swift layout.swift <mounted volume> <width>x<height> <icon size> <name>=<x>,<y>...
//   The size is the window's content, below the title bar. Each position is the centre of that
//   item's icon, in points from the content's top left.
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("layout.swift: \(message)\n".utf8))
    exit(1)
}

let arguments = CommandLine.arguments
guard arguments.count >= 5,
      case let size = arguments[2].split(separator: "x").compactMap({ Int($0) }), size.count == 2,
      let iconSize = Double(arguments[3]) else {
    fail("usage: swift layout.swift <mounted volume> <width>x<height> <icon size> <name>=<x>,<y>...")
}
let volume = URL(fileURLWithPath: arguments[1], isDirectory: true)
let background = volume.appendingPathComponent(".background/background.tiff")
let items: [(name: String, x: UInt32, y: UInt32)] = arguments.dropFirst(4).map { argument in
    let parts = argument.split(separator: "=", maxSplits: 1)
    let position = parts.count == 2 ? parts[1].split(separator: ",").compactMap({ UInt32($0) }) : []
    guard position.count == 2 else { fail("\(argument) is not <name>=<x>,<y>") }
    return (String(parts[0]), position[0], position[1])
}

extension Data {
    mutating func u16(_ value: UInt16) { append(contentsOf: [UInt8(value >> 8), UInt8(value & 0xFF)]) }
    mutating func u32(_ value: UInt32) { u16(UInt16(value >> 16)); u16(UInt16(value & 0xFFFF)) }
    mutating func u64(_ value: UInt64) { u32(UInt32(value >> 32)); u32(UInt32(value & 0xFFFF_FFFF)) }
    mutating func code(_ code: String) { precondition(code.utf8.count == 4); append(contentsOf: Array(code.utf8)) }
    mutating func zeros(_ count: Int) { append(Data(count: count)) }
    /// A Pascal string in a field of `width` bytes: a length byte, then the bytes, zero-padded.
    mutating func pascal(_ bytes: Data, width: Int) {
        let kept = bytes.prefix(width - 1)
        append(UInt8(kept.count))
        append(kept)
        zeros(width - 1 - kept.count)
    }
    mutating func utf16(_ text: String) { for unit in text.utf16 { u16(unit) } }
}

func attributes(_ url: URL) -> [FileAttributeKey: Any] {
    guard let attributes = try? FileManager.default.attributesOfItem(atPath: url.path) else { fail("cannot read \(url.path)") }
    return attributes
}
func fileNumber(_ url: URL) -> UInt32 {
    guard let number = attributes(url)[.systemFileNumber] as? NSNumber else { fail("no file number for \(url.path)") }
    return number.uint32Value
}
/// Seconds since 1904, the classic Mac OS epoch.
func macTime(_ url: URL) -> Double {
    guard let date = attributes(url)[.creationDate] as? Date else { fail("no creation date for \(url.path)") }
    return date.timeIntervalSince1970 + 2_082_844_800
}

// An alias record (version 2) for the background, as Finder writes them: a fixed 150-byte part,
// then tagged fields. On HFS+ a file number is the catalog node ID an alias refers to.
func aliasRecord(to file: URL, on volume: URL) -> Data {
    guard let volumeName = try? volume.resourceValues(forKeys: [.volumeNameKey]).volumeName else {
        fail("cannot read the name of \(volume.path)")
    }
    let folder = file.deletingLastPathComponent()
    let carbon = { (text: String) in Data(text.replacingOccurrences(of: ":", with: "/").utf8) }
    let volumeTime = macTime(volume), fileTime = macTime(file)
    let relative = String(file.path.dropFirst(volume.path.count))

    var record = Data()
    record.zeros(4)                           // application info
    record.u16(0)                             // length, filled in below
    record.u16(2)                             // version
    record.u16(0)                             // a file, not a folder
    record.pascal(carbon(volumeName), width: 28)
    record.u32(UInt32(volumeTime))
    record.append(contentsOf: Array("H+".utf8)) // HFS+
    record.u16(0)                             // a fixed disk
    record.u32(fileNumber(folder))
    record.pascal(carbon(file.lastPathComponent), width: 64)
    record.u32(fileNumber(file))
    record.u32(UInt32(fileTime))
    record.zeros(8)                           // creator and type codes
    record.u16(0xFFFF)                        // levels up from the alias to a common folder: none
    record.u16(0xFFFF)                        // and down to the target
    record.zeros(4 + 2 + 10)                  // volume attributes, file system ID, reserved

    func tag(_ tag: UInt16, _ value: Data) {
        record.u16(tag)
        record.u16(UInt16(value.count))
        record.append(value)
        if value.count % 2 == 1 { record.zeros(1) }
    }
    func unicode(_ text: String) -> Data {
        var value = Data()
        value.u16(UInt16(text.utf16.count))
        value.utf16(text.replacingOccurrences(of: ":", with: "/"))
        return value
    }
    tag(0, carbon(folder.lastPathComponent))                                     // folder name
    var times = Data()
    times.u64(UInt64(volumeTime * 65536))
    tag(16, times)                                                               // volume created
    times = Data()
    times.u64(UInt64(fileTime * 65536))
    tag(17, times)                                                               // file created
    var folders = Data()
    folders.u32(fileNumber(folder))
    tag(1, folders)                                                              // folder IDs
    tag(2, carbon(volumeName) + Data(":".utf8) + carbon(folder.lastPathComponent) + Data(":\0".utf8)
           + carbon(file.lastPathComponent))                                     // Carbon path
    tag(14, unicode(file.lastPathComponent))                                     // file name
    tag(15, unicode(volumeName))                                                 // volume name
    tag(18, Data(relative.utf8))                                                 // path in the volume
    tag(19, Data(volume.path.utf8))                                              // where it was mounted
    record.u16(0xFFFF)                                                           // the end
    record.u16(0)
    record.replaceSubrange(4..<6, with: [UInt8(record.count >> 8), UInt8(record.count & 0xFF)])
    return record
}

func plist(_ object: [String: Any]) -> Data {
    guard let data = try? PropertyListSerialization.data(fromPropertyList: object, format: .binary, options: 0) else {
        fail("cannot encode the window settings")
    }
    return data
}

// The records: name, code, then the value's type and bytes.
struct Record { let name: String; let code: String; let value: Data }
func blob(_ bytes: Data) -> Data { var value = Data(); value.code("blob"); value.u32(UInt32(bytes.count)); value.append(bytes); return value }
func long(_ number: UInt32) -> Data { var value = Data(); value.code("long"); value.u32(number); return value }
func type(_ code: String) -> Data { var value = Data(); value.code("type"); value.code(code); return value }

guard let bookmark = try? background.bookmarkData() else { fail("cannot make a bookmark to \(background.path)") }
// Finder's window bounds count from the bottom left of the main screen and include the title bar
// (28 points since macOS 11). 400, 340 puts the window near the middle of a 1440 by 900 screen.
var records = [
    Record(name: ".", code: "bwsp", value: blob(plist([
        "WindowBounds": "{{400, 340}, {\(size[0]), \(size[1] + 28)}}",
        "ShowToolbar": false, "ShowSidebar": false, "ContainerShowSidebar": false, "SidebarWidth": 180,
        "ShowStatusBar": false, "ShowPathbar": false, "ShowTabView": false, "PreviewPaneVisibility": false,
    ]))),
    Record(name: ".", code: "icvl", value: type("icnv")),
    Record(name: ".", code: "icvp", value: blob(plist([
        "viewOptionsVersion": 1, "backgroundType": 2, "backgroundImageAlias": aliasRecord(to: background, on: volume),
        "backgroundColorRed": 0.04, "backgroundColorGreen": 0.07, "backgroundColorBlue": 0.13,
        "iconSize": iconSize, "textSize": 13.0, "labelOnBottom": true, "arrangeBy": "none",
        "gridSpacing": 100.0, "gridOffsetX": 0.0, "gridOffsetY": 0.0, "scrollPositionX": 0.0, "scrollPositionY": 0.0,
        "showIconPreview": true, "showItemInfo": false,
    ]))),
    Record(name: ".", code: "pBBk", value: blob(bookmark)),
    Record(name: ".", code: "vSrn", value: long(1)),
]
for item in items {
    var location = Data()
    location.u32(item.x)
    location.u32(item.y)
    location.u32(0xFFFF_FFFF)
    location.u32(0xFFFF_0000)
    records.append(Record(name: item.name, code: "Iloc", value: blob(location)))
}
// Finder keeps records sorted by name, ignoring case, then by code.
records.sort { ($0.name.lowercased(), $0.code) < ($1.name.lowercased(), $1.code) }

// One leaf node holds every record; it must fit in a page.
let pageSize = 0x1000
var leaf = Data()
leaf.u32(0)                                   // no child node: a leaf
leaf.u32(UInt32(records.count))
for record in records {
    leaf.u32(UInt32(record.name.utf16.count))
    leaf.utf16(record.name)
    leaf.code(record.code)
    leaf.append(record.value)
}
guard leaf.count <= pageSize else { fail("the records take \(leaf.count) bytes, more than a page") }

// Blocks, at offsets counted from byte 4 of the file: the 32-byte header at 0, the B-tree's own
// header ("DSDB") at 0x20, the allocator's table of blocks at 0x800 and the leaf at 0x1000. Every
// other part of the 2 GB address space goes on the free lists, split into power-of-two buddies.
let blocks: [(offset: Int, size: Int)] = [(0x800, 0x800), (0x20, 0x20), (0x1000, pageSize)]
let used = [(offset: 0, size: 0x20)] + blocks
var free = Array(repeating: [UInt32](), count: 32)
func carve(_ offset: Int, _ size: Int) {
    if used.contains(where: { $0.offset == offset && $0.size == size }) { return }
    if !used.contains(where: { $0.offset < offset + size && offset < $0.offset + $0.size }) {
        free[size.trailingZeroBitCount].append(UInt32(offset))
        return
    }
    carve(offset, size / 2)
    carve(offset + size / 2, size / 2)
}
carve(0, 1 << 31)

var table = Data()
table.u32(UInt32(blocks.count))
table.u32(0)
for block in blocks { table.u32(UInt32(block.offset | block.size.trailingZeroBitCount)) }
table.zeros(4 * (256 - blocks.count))         // the block list is padded to 256 entries
table.u32(1)                                  // the table of contents: one name
table.append(4)
table.code("DSDB")
table.u32(1)                                  // block 1
for list in free {
    table.u32(UInt32(list.count))
    for offset in list { table.u32(offset) }
}

var tree = Data()
tree.u32(2)                                   // the root node: block 2
tree.u32(0)                                   // levels above the leaves
tree.u32(UInt32(records.count))
tree.u32(1)                                   // nodes
tree.u32(UInt32(pageSize))

var file = Data(count: 4 + 0x2000)
func put(_ bytes: Data, at offset: Int) { file.replaceSubrange((4 + offset)..<(4 + offset + bytes.count), with: bytes) }
var header = Data()
header.code("Bud1")
header.u32(0x800)                             // the table of blocks: offset, size, offset again
header.u32(0x800)
header.u32(0x800)
header.zeros(16)
file.replaceSubrange(0..<4, with: [0, 0, 0, 1])
put(header, at: 0)
put(tree, at: 0x20)
put(table, at: 0x800)
put(leaf, at: 0x1000)
do { try file.write(to: volume.appendingPathComponent(".DS_Store")) } catch { fail("cannot write the .DS_Store: \(error)") }
