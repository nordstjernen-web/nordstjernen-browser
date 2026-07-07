// Nordstjernen — Swift wrapper over the libnordstjernen C embedding API.

import Foundation
import UIKit

/// A single open page. All engine calls run on a private serial queue because
/// open/tick/render mutate one non-thread-safe engine instance; results are
/// delivered back on the main queue.
final class BrowserEngine {
    static let shared = BrowserEngine()

    private let queue = DispatchQueue(label: "org.nordstjernen.engine")
    private var browser: OpaquePointer?
    private var didInit = false

    private init() {}

    /// One-time engine init. Points the engine at a writable directory and the
    /// bundled CA bundle, mirroring the Android host's nativeInit.
    func start() {
        queue.sync {
            guard !didInit else { return }
            let dataDir = Self.dataDirectory()
            let ca = Bundle.main.path(forResource: "cacert", ofType: "pem")
            didInit = ns_ios_init(dataDir, ca) == 0
        }
    }

    private static func dataDirectory() -> String {
        let base = FileManager.default.urls(for: .libraryDirectory,
                                            in: .userDomainMask).first
        let dir = base?.appendingPathComponent("nordstjernen", isDirectory: true)
        if let dir = dir {
            try? FileManager.default.createDirectory(at: dir,
                                                     withIntermediateDirectories: true)
            return dir.path
        }
        return NSTemporaryDirectory()
    }

    struct Page {
        var width: Int
        var height: Int
        var title: String?
        var url: String?
    }

    /// Open a URL at the given CSS viewport width. Completion runs on main.
    func open(_ url: String, viewportWidth: Int, viewportHeight: Double,
              settleMs: Int32 = 1500, completion: @escaping (Page?) -> Void) {
        queue.async {
            self.start()
            if let existing = self.browser {
                ns_browser_close(existing)
                self.browser = nil
            }
            let b = ns_browser_open_viewport(url, Int32(viewportWidth),
                                             viewportHeight, settleMs)
            self.browser = b
            let page = b.map { self.snapshot($0) }
            DispatchQueue.main.async { completion(page) }
        }
    }

    private func snapshot(_ b: OpaquePointer) -> Page {
        var w: Int32 = 0, h: Int32 = 0
        _ = ns_browser_page_size(b, &w, &h)
        return Page(width: Int(w), height: Int(h),
                    title: Self.takeString(ns_browser_title(b)),
                    url: Self.takeString(ns_browser_url(b)))
    }

    /// Advance live work (timers, fetches, animations) for up to budgetMs.
    func tick(budgetMs: Int32 = 16, completion: ((Bool) -> Void)? = nil) {
        queue.async {
            let changed = self.browser.map { ns_browser_tick($0, budgetMs) != 0 } ?? false
            if let completion = completion {
                DispatchQueue.main.async { completion(changed) }
            }
        }
    }

    /// Render a device-pixel viewport region into a UIImage. scale maps CSS
    /// pixels to device pixels (the screen density).
    func render(scrollX: Int, scrollY: Int, width: Int, height: Int,
                scale: CGFloat, completion: @escaping (UIImage?) -> Void) {
        queue.async {
            guard let b = self.browser, width > 0, height > 0 else {
                DispatchQueue.main.async { completion(nil) }
                return
            }
            let stride = width * 4
            var buffer = [UInt8](repeating: 0, count: stride * height)
            let rc = buffer.withUnsafeMutableBufferPointer { ptr -> Int32 in
                ns_browser_render_argb32(b, Int32(scrollX), Int32(scrollY),
                                         Int32(width), Int32(height),
                                         Double(scale), ptr.baseAddress, Int32(stride))
            }
            let image = rc == 0 ? Self.image(from: buffer, width: width,
                                             height: height, stride: stride,
                                             scale: scale) : nil
            DispatchQueue.main.async { completion(image) }
        }
    }

    /// Cairo ARGB32 (premultiplied, host byte order) → CGImage. On little-endian
    /// arm64 that is byteOrder32Little + premultipliedFirst (ARGB).
    private static func image(from buffer: [UInt8], width: Int, height: Int,
                              stride: Int, scale: CGFloat) -> UIImage? {
        let data = Data(buffer)
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        let info: CGBitmapInfo = [.byteOrder32Little,
                                  CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)]
        guard let cg = CGImage(width: width, height: height,
                               bitsPerComponent: 8, bitsPerPixel: 32,
                               bytesPerRow: stride,
                               space: CGColorSpaceCreateDeviceRGB(),
                               bitmapInfo: info, provider: provider,
                               decode: nil, shouldInterpolate: false,
                               intent: .defaultIntent) else { return nil }
        return UIImage(cgImage: cg, scale: scale, orientation: .up)
    }

    /// Absolute URL of the link at CSS coordinates, or nil.
    func linkAt(x: Int, y: Int, completion: @escaping (String?) -> Void) {
        queue.async {
            let s = self.browser.flatMap { Self.takeString(ns_browser_link_at($0, Int32(x), Int32(y))) }
            DispatchQueue.main.async { completion(s) }
        }
    }

    /// Dispatch a click; completion gets the navigation URL the click produced.
    func click(x: Int, y: Int, completion: @escaping (String?) -> Void) {
        queue.async {
            let nav = self.browser.flatMap { Self.takeString(ns_browser_click($0, Int32(x), Int32(y), 0)) }
            DispatchQueue.main.async { completion(nav) }
        }
    }

    func setViewportWidth(_ width: Int) {
        queue.async {
            if let b = self.browser { _ = ns_browser_set_viewport_width(b, Int32(width)) }
        }
    }

    func close() {
        queue.async {
            if let b = self.browser { ns_browser_close(b); self.browser = nil }
        }
    }

    /// Convert an engine-owned char* (which the caller must free) to a Swift
    /// String, freeing the C allocation.
    private static func takeString(_ c: UnsafeMutablePointer<CChar>?) -> String? {
        guard let c = c else { return nil }
        defer { free(c) }
        let s = String(cString: c)
        return s.isEmpty ? nil : s
    }
}
