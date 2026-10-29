// The FAF client, set up by FAF Native: FAF's own Unix build (GitHub releases, MIT), run on a Java
// runtime for Intel Macs (Eclipse Temurin, under Rosetta: some of the client's native helpers,
// Steam and Discord, exist only for x86_64), with its Linux JavaFX libraries swapped for the macOS
// ones (OpenJFX from Maven Central). Every download is checked against the checksum its source
// publishes. The client lives in Places.clientInstall; FAF Native starts it with the Java options
// that work for it on a Mac (no Terminal). It keeps its own settings and data in ~/.faforever as
// usual; FAF Native writes two of those settings only when asked ("Apply"), after a backup.
import CryptoKit
import Foundation

extension Places {
    static let clientInstall = appSupport.appendingPathComponent("FAF Client", isDirectory: true)
    static let clientPrefs = clientData.appendingPathComponent("client.prefs")
    static let rosetta = URL(fileURLWithPath: "/Library/Apple/usr/share/rosetta/rosetta")
}

/// What Places.clientInstall holds (installed.json there).
struct InstalledClient: Codable, Equatable {
    var clientVersion: String   // the release tag without its "v", e.g. 2026.7.1
    var javaVersion: String
    var javafxVersion: String
    var folder: String          // the client's folder inside Places.clientInstall, e.g. faf-client-2026.7.1

    static var file: URL { Places.clientInstall.appendingPathComponent("installed.json") }

    static func load() -> InstalledClient? {
        guard let data = try? Data(contentsOf: file) else { return nil }
        return try? JSONDecoder().decode(InstalledClient.self, from: data)
    }

    var clientFolder: URL { Places.clientInstall.appendingPathComponent(folder, isDirectory: true) }
    var java: URL { Places.clientInstall.appendingPathComponent("java/Contents/Home/bin/java") }
}

/// A file to download and the checksum to hold it to.
struct Download {
    enum Digest { case sha256(String), sha1(String) }
    let url: URL
    let name: String
    let size: Int64
    let digest: Digest
}

/// The newest client release and the matching Java runtime, from their sources' APIs.
struct ClientRelease {
    static let javaFeature = 25   // the Java version FAF's client is built for (2026.7.1)
    let version: String
    let client: Download
    let java: Download
    let javaVersion: String

    static func fetch() async throws -> ClientRelease {
        let session = URLSession(configuration: .ephemeral)
        // FAF's GitHub releases: the Unix build, with the SHA-256 GitHub records for each asset.
        let (releaseData, _) = try await session.data(from: URL(string: "https://api.github.com/repos/FAForever/downlords-faf-client/releases/latest")!)
        guard let release = try JSONSerialization.jsonObject(with: releaseData) as? [String: Any],
              let tag = release["tag_name"] as? String,
              let assets = release["assets"] as? [[String: Any]],
              let asset = assets.first(where: { ($0["name"] as? String).map { $0.hasPrefix("faf_unix_") && $0.hasSuffix(".tar.gz") } ?? false }),
              let name = asset["name"] as? String, let link = asset["browser_download_url"] as? String,
              let url = URL(string: link), let digest = asset["digest"] as? String, digest.hasPrefix("sha256:")
        else { throw SetupError("FAF's latest client release has no Unix build with a checksum.") }
        let client = Download(url: url, name: name, size: (asset["size"] as? NSNumber)?.int64Value ?? 0,
                              digest: .sha256(String(digest.dropFirst("sha256:".count))))
        // Adoptium: the newest Temurin JRE of that Java version for Intel Macs.
        let javaAPI = "https://api.adoptium.net/v3/assets/latest/\(javaFeature)/hotspot?architecture=x64&image_type=jre&os=mac&vendor=eclipse"
        let (javaData, _) = try await session.data(from: URL(string: javaAPI)!)
        guard let builds = try JSONSerialization.jsonObject(with: javaData) as? [[String: Any]],
              let build = builds.first, let binary = build["binary"] as? [String: Any],
              let package = binary["package"] as? [String: Any],
              let javaName = package["name"] as? String, let javaLink = package["link"] as? String,
              let javaURL = URL(string: javaLink), let checksum = package["checksum"] as? String,
              let versionInfo = build["version"] as? [String: Any], let semver = versionInfo["semver"] as? String
        else { throw SetupError("Adoptium has no Java \(javaFeature) runtime for Intel Macs.") }
        let java = Download(url: javaURL, name: javaName, size: (package["size"] as? NSNumber)?.int64Value ?? 0,
                            digest: .sha256(checksum))
        return ClientRelease(version: tag.hasPrefix("v") ? String(tag.dropFirst()) : tag, client: client, java: java,
                             javaVersion: String(semver.prefix { $0 != "+" }))   // 25.0.5+8.0.LTS -> 25.0.5
    }
}

struct SetupError: LocalizedError {
    let errorDescription: String?
    init(_ text: String) { errorDescription = text }
}

/// Downloads one file with progress, into a temporary location the caller then owns.
final class Downloader: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
    private var continuation: CheckedContinuation<URL, Error>?
    private let progress: (Int64) -> Void
    private let keepAt: URL

    private init(keepAt: URL, progress: @escaping (Int64) -> Void) {
        self.keepAt = keepAt
        self.progress = progress
    }

    static func fetch(_ url: URL, to destination: URL, progress: @escaping (Int64) -> Void) async throws {
        let downloader = Downloader(keepAt: destination, progress: progress)
        let session = URLSession(configuration: .ephemeral, delegate: downloader, delegateQueue: nil)
        defer { session.finishTasksAndInvalidate() }
        _ = try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<URL, Error>) in
                downloader.continuation = continuation
                session.downloadTask(with: url).resume()
            }
        } onCancel: {
            session.invalidateAndCancel()
        }
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData _: Int64,
                    totalBytesWritten: Int64, totalBytesExpectedToWrite _: Int64) {
        progress(totalBytesWritten)
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        // The temporary file is deleted when this returns: move it now.
        if let http = downloadTask.response as? HTTPURLResponse, !(200..<300).contains(http.statusCode) {
            finish(.failure(SetupError("\(downloadTask.originalRequest?.url?.host ?? "The server") answered \(http.statusCode).")))
            return
        }
        do {
            try? FileManager.default.removeItem(at: keepAt)
            try FileManager.default.moveItem(at: location, to: keepAt)
            finish(.success(keepAt))
        } catch {
            finish(.failure(error))
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        if let error { finish(.failure(error)) }
    }

    private func finish(_ result: Result<URL, Error>) {
        continuation?.resume(with: result)
        continuation = nil
    }
}

/// Builds the client into Places.clientInstall: downloads, checks, unpacks, swaps JavaFX, and
/// replaces any earlier install only once the new one is complete.
enum ClientInstaller {
    static func file(_ url: URL, matches digest: Download.Digest) throws -> Bool {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        var sha256 = SHA256()
        var sha1 = Insecure.SHA1()
        while let chunk = try handle.read(upToCount: 4 << 20), !chunk.isEmpty {
            switch digest {
            case .sha256: sha256.update(data: chunk)
            case .sha1: sha1.update(data: chunk)
            }
        }
        let hex: String
        let expected: String
        switch digest {
        case .sha256(let value): hex = sha256.finalize().map { String(format: "%02x", $0) }.joined(); expected = value
        case .sha1(let value): hex = sha1.finalize().map { String(format: "%02x", $0) }.joined(); expected = value
        }
        return hex == expected.lowercased().trimmingCharacters(in: .whitespacesAndNewlines)
    }

    static func run(_ tool: String, _ arguments: [String]) throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: tool)
        process.arguments = arguments
        process.standardOutput = FileHandle.nullDevice
        let errors = Pipe()
        process.standardError = errors
        try process.run()
        process.waitUntilExit()
        if process.terminationStatus != 0 {
            let text = String(data: errors.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
            throw SetupError("\((tool as NSString).lastPathComponent) failed: \(text.trimmingCharacters(in: .whitespacesAndNewlines))")
        }
    }

    /// The OpenJFX jars that replace the client's Linux ones: name, version, from lib/.
    static func linuxJavaFX(in lib: URL) throws -> [(module: String, version: String, file: URL)] {
        try FileManager.default.contentsOfDirectory(at: lib, includingPropertiesForKeys: nil).compactMap { url in
            // javafx-<module>-<version>-linux.jar
            let name = url.lastPathComponent
            guard name.hasPrefix("javafx-"), name.hasSuffix("-linux.jar") else { return nil }
            let parts = name.dropFirst("javafx-".count).dropLast("-linux.jar".count).split(separator: "-")
            guard parts.count == 2 else { return nil }
            return (String(parts[0]), String(parts[1]), url)
        }
    }

    /// `status(text, fraction)` reports each step; throws CancellationError when the task is cancelled.
    static func install(_ release: ClientRelease, status: @escaping (String, Double) -> Void) async throws {
        let fm = FileManager.default
        try fm.createDirectory(at: Places.appSupport, withIntermediateDirectories: true)
        let work = Places.appSupport.appendingPathComponent("FAF Client.partial", isDirectory: true)
        try? fm.removeItem(at: work)
        try fm.createDirectory(at: work, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: work) }

        // 1. The client and Java (about 90% of the bytes).
        let total = Double(release.client.size + release.java.size) * 1.25
        var done: Int64 = 0
        for item in [release.client, release.java] {
            let label = item.name == release.client.name ? "the FAF client \(release.version)" : "Java \(release.javaVersion)"
            let target = work.appendingPathComponent(item.name)
            let before = done
            try await Downloader.fetch(item.url, to: target) { written in
                status(String(format: "Downloading %@ (%.0f of %.0f MB)", label, Double(written) / 1e6, Double(item.size) / 1e6),
                       Double(before + written) / total)
            }
            try Task.checkCancellation()
            status("Checking \(label)", Double(before + item.size) / total)
            guard try file(target, matches: item.digest) else {
                throw SetupError("\(item.name) doesn't match its published checksum; nothing was installed.")
            }
            done += item.size
        }

        // 2. Unpack both.
        status("Unpacking", Double(done) / total)
        let staged = work.appendingPathComponent("FAF Client", isDirectory: true)
        try fm.createDirectory(at: staged, withIntermediateDirectories: true)
        try run("/usr/bin/tar", ["-xzf", work.appendingPathComponent(release.client.name).path, "-C", staged.path])
        let javaUnpack = work.appendingPathComponent("java-unpack", isDirectory: true)
        try fm.createDirectory(at: javaUnpack, withIntermediateDirectories: true)
        try run("/usr/bin/tar", ["-xzf", work.appendingPathComponent(release.java.name).path, "-C", javaUnpack.path])
        guard let javaTop = try fm.contentsOfDirectory(at: javaUnpack, includingPropertiesForKeys: nil).first(where: {
            fm.fileExists(atPath: $0.appendingPathComponent("Contents/Home/bin/java").path)
        }) else { throw SetupError("The Java download has no Contents/Home/bin/java.") }
        try fm.moveItem(at: javaTop, to: staged.appendingPathComponent("java"))
        guard let clientTop = try fm.contentsOfDirectory(at: staged, includingPropertiesForKeys: nil).first(where: {
            $0.lastPathComponent.hasPrefix("faf-client") && fm.fileExists(atPath: $0.appendingPathComponent("lib").path)
        }) else { throw SetupError("The client download has no faf-client folder with a lib folder.") }

        // 3. JavaFX: the macOS (x86_64, "mac" classifier) jars of the same version replace the Linux ones.
        let lib = clientTop.appendingPathComponent("lib")
        let linux = try linuxJavaFX(in: lib)
        guard let javafxVersion = linux.first?.version else { throw SetupError("The client has no JavaFX libraries to replace.") }
        let aside = clientTop.appendingPathComponent("lib-linux-javafx", isDirectory: true)
        try fm.createDirectory(at: aside, withIntermediateDirectories: true)
        for (index, jar) in linux.enumerated() {
            try Task.checkCancellation()
            let name = "javafx-\(jar.module)-\(jar.version)-mac.jar"
            let base = "https://repo1.maven.org/maven2/org/openjfx/javafx-\(jar.module)/\(jar.version)/\(name)"
            status("Downloading the macOS JavaFX libraries (\(index + 1) of \(linux.count))",
                   (Double(done) + Double(index) / Double(linux.count) * total * 0.2) / total)
            let (sumData, _) = try await URLSession(configuration: .ephemeral).data(from: URL(string: base + ".sha1")!)
            let expected = String(decoding: sumData, as: UTF8.self).split(separator: " ").first.map(String.init) ?? ""
            let target = lib.appendingPathComponent(name)
            try await Downloader.fetch(URL(string: base)!, to: target) { _ in }
            guard expected.count == 40, try file(target, matches: .sha1(expected)) else {
                throw SetupError("\(name) doesn't match its published checksum; nothing was installed.")
            }
            try fm.moveItem(at: jar.file, to: aside.appendingPathComponent(jar.file.lastPathComponent))
        }

        // 4. Java must start (under Rosetta).
        status("Checking that Java starts", 0.98)
        try run(staged.appendingPathComponent("java/Contents/Home/bin/java").path, ["-version"])

        // 5. Record it, then put it in place of any earlier install.
        let installed = InstalledClient(clientVersion: release.version, javaVersion: release.javaVersion,
                                        javafxVersion: javafxVersion, folder: clientTop.lastPathComponent)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(installed).write(to: staged.appendingPathComponent("installed.json"))
        try Task.checkCancellation()
        let old = Places.appSupport.appendingPathComponent("FAF Client.old", isDirectory: true)
        try? fm.removeItem(at: old)
        if fm.fileExists(atPath: Places.clientInstall.path) { try fm.moveItem(at: Places.clientInstall, to: old) }
        try fm.moveItem(at: staged, to: Places.clientInstall)
        try? fm.removeItem(at: old)
        status("Installed", 1)
    }
}

/// Starting the installed client, and whether a client is running.
enum ClientRunner {
    static func isRunning() -> Bool {
        // A test home (snapshot_setup.sh) is not the one a running client uses.
        if ProcessInfo.processInfo.environment["FAF_NATIVE_TEST_HOME"] != nil { return false }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/pgrep")
        process.arguments = ["-f", "com.faforever.client.Main"]
        process.standardOutput = FileHandle.nullDevice
        do { try process.run() } catch { return false }
        process.waitUntilExit()
        return process.terminationStatus == 0
    }

    /// The options that work for the client on a Mac (as ~/Gaming/faf/run-faf.command used them).
    static func start(_ installed: InstalledClient) throws {
        let fm = FileManager.default
        let home = installed.clientFolder
        let lib = home.appendingPathComponent("lib")
        let i4j = home.appendingPathComponent(".install4j")
        let jna = ((try? fm.contentsOfDirectory(atPath: lib.path)) ?? []).filter { $0.hasPrefix("jna-") && $0.hasSuffix(".jar") }.sorted()
        let launcher = ((try? fm.contentsOfDirectory(atPath: i4j.path)) ?? []).filter { $0.hasPrefix("launcher") && $0.hasSuffix(".jar") }
        let classpath = jna.map { lib.appendingPathComponent($0).path }
            + [i4j.appendingPathComponent("i4jruntime.jar").path] + launcher.map { i4j.appendingPathComponent($0).path }
            + [lib.path + "/*"]
        try fm.createDirectory(at: Places.logs, withIntermediateDirectories: true)
        let logURL = Places.logs.appendingPathComponent("faf-client.log")
        if !fm.fileExists(atPath: logURL.path) { fm.createFile(atPath: logURL.path, contents: nil) }
        let log = try FileHandle(forWritingTo: logURL)
        try log.seekToEnd()
        let process = Process()
        process.executableURL = installed.java
        process.currentDirectoryURL = home
        process.arguments = [
            "-Xmx1g", "-XX:+HeapDumpOnOutOfMemoryError", "-Djava.net.preferIPv4Stack=true", "-DnativeDir=natives",
            "-Djava.library.path=.", "-Dio.netty.allocator.type=unpooled", "--add-opens=java.base/java.lang=ALL-UNNAMED",
            "--enable-native-access=ALL-UNNAMED", "-Dapple.awt.application.name=FAF Client",
            "-classpath", classpath.joined(separator: ":"), "install4j.com.faforever.client.Main",
        ]
        process.standardOutput = log
        process.standardError = log
        try process.run()
    }
}

/// The two client settings FAF Native needs, written into client.prefs on request.
enum ClientSettings {
    /// Sets the executable decorator (and the Forged Alliance location, when given), keeping every
    /// other setting; the old file is kept beside it as client.prefs.before-faf-native-<time>.
    static func apply(gamePath: String?) throws -> URL {
        guard !ClientRunner.isRunning() else { throw SetupError("Quit the FAF client first: it rewrites its settings when it quits.") }
        let data = try Data(contentsOf: Places.clientPrefs)
        guard var root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw SetupError("The client's settings file isn't the JSON FAF Native expects; nothing was changed.")
        }
        var fa = root["forgedAlliance"] as? [String: Any] ?? [:]
        fa["executableDecorator"] = Places.launcherLine
        if let gamePath { fa["installationPath"] = gamePath }
        root["forgedAlliance"] = fa
        let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-")
        let backup = Places.clientPrefs.deletingLastPathComponent().appendingPathComponent("client.prefs.before-faf-native-\(stamp)")
        try FileManager.default.copyItem(at: Places.clientPrefs, to: backup)
        let updated = try JSONSerialization.data(withJSONObject: root, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
        try updated.write(to: Places.clientPrefs, options: .atomic)
        return backup
    }
}
