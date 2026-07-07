// Nordstjernen — scrolling surface that renders the engine's viewport output
// and re-renders as the user scrolls, mirroring the Android PageView.

import UIKit

protocol PageViewDelegate: AnyObject {
    func pageView(_ pageView: PageView, didNavigateTo url: String)
    func pageViewDidUpdateMetadata(_ pageView: PageView)
}

final class PageView: UIScrollView, UIScrollViewDelegate {
    weak var pageDelegate: PageViewDelegate?

    private let tile = UIImageView()
    private var pageCSSWidth: CGFloat = 0
    private var pageCSSHeight: CGFloat = 0
    private var renderScheduled = false

    override init(frame: CGRect) {
        super.init(frame: frame)
        commonInit()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        commonInit()
    }

    private func commonInit() {
        delegate = self
        backgroundColor = .white
        contentInsetAdjustmentBehavior = .never
        showsVerticalScrollIndicator = true
        tile.contentMode = .topLeft
        addSubview(tile)

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        addGestureRecognizer(tap)
    }

    /// The CSS viewport width the engine should lay out for: the view width in
    /// points (≈ mobile CSS pixels).
    var cssViewportWidth: Int { max(1, Int(bounds.width)) }

    func loadPage(width: Int, height: Int) {
        pageCSSWidth = CGFloat(width)
        pageCSSHeight = CGFloat(height)
        contentSize = CGSize(width: pageCSSWidth, height: max(pageCSSHeight, bounds.height))
        setContentOffset(.zero, animated: false)
        scheduleRender()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        if pageCSSHeight > 0 {
            contentSize = CGSize(width: bounds.width,
                                 height: max(pageCSSHeight, bounds.height))
        }
        scheduleRender()
    }

    func scrollViewDidScroll(_ scrollView: UIScrollView) {
        scheduleRender()
    }

    /// Coalesce render requests to one per run-loop pass so a scroll gesture
    /// does not queue a backlog of renders.
    private func scheduleRender() {
        guard !renderScheduled, bounds.width > 0, bounds.height > 0 else { return }
        renderScheduled = true
        DispatchQueue.main.async { [weak self] in
            self?.renderScheduled = false
            self?.renderVisible()
        }
    }

    private func renderVisible() {
        let scale = window?.screen.scale ?? UIScreen.main.scale
        let visible = CGRect(origin: contentOffset, size: bounds.size)
        let originX = Int(max(0, visible.minX))
        let originY = Int(max(0, visible.minY))
        let w = Int(bounds.width)
        let h = Int(bounds.height)
        BrowserEngine.shared.render(scrollX: originX, scrollY: originY,
                                    width: Int(CGFloat(w) * scale),
                                    height: Int(CGFloat(h) * scale),
                                    scale: scale) { [weak self] image in
            guard let self = self, let image = image else { return }
            self.tile.image = image
            self.tile.frame = CGRect(x: CGFloat(originX), y: CGFloat(originY),
                                     width: CGFloat(w), height: CGFloat(h))
        }
    }

    @objc private func handleTap(_ gesture: UITapGestureRecognizer) {
        let p = gesture.location(in: self)
        let x = Int(p.x)
        let y = Int(p.y)
        BrowserEngine.shared.click(x: x, y: y) { [weak self] nav in
            guard let self = self else { return }
            if let nav = nav, !nav.isEmpty {
                self.pageDelegate?.pageView(self, didNavigateTo: nav)
            } else {
                self.scheduleRender()
            }
        }
    }
}
