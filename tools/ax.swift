// ax dump <pid> [depth] | ax press <pid> <text> [role] : accessibility actions sent to the app, no mouse events.
import ApplicationServices
import Foundation
import AppKit

func attr(_ e: AXUIElement, _ name: String) -> AnyObject? {
  var v: AnyObject?
  return AXUIElementCopyAttributeValue(e, name as CFString, &v) == .success ? v : nil
}
func str(_ e: AXUIElement, _ name: String) -> String { (attr(e, name) as? String) ?? "" }
func children(_ e: AXUIElement) -> [AXUIElement] { (attr(e, kAXChildrenAttribute) as? [AXUIElement]) ?? [] }
func label(_ e: AXUIElement) -> String {
  let v = attr(e, kAXValueAttribute)
  return "\(str(e, kAXRoleAttribute)) title='\(str(e, kAXTitleAttribute))' desc='\(str(e, kAXDescriptionAttribute))' value='\(v.map { "\($0)" } ?? "")'"
}
func walk(_ e: AXUIElement, _ depth: Int, _ maxDepth: Int, _ visit: (AXUIElement, Int) -> Bool) -> Bool {
  if visit(e, depth) { return true }
  if depth >= maxDepth { return false }
  for c in children(e) { if walk(c, depth + 1, maxDepth, visit) { return true } }
  return false
}

let args = CommandLine.arguments
guard args.count >= 3, let pid = pid_t(args[2]) else { print("usage"); exit(2) }
print("trusted:", AXIsProcessTrusted())
let app = AXUIElementCreateApplication(pid)

switch args[1] {
case "dump":
  let maxDepth = args.count > 3 ? Int(args[3])! : 12
  _ = walk(app, 0, maxDepth) { e, d in print(String(repeating: "  ", count: d) + label(e)); return false }
case "press":
  let text = args[3]
  let role = args.count > 4 ? args[4] : nil
  var done = false
  _ = walk(app, 0, 30) { e, _ in
    let matches = [str(e, kAXTitleAttribute), str(e, kAXDescriptionAttribute), (attr(e, kAXValueAttribute) as? String) ?? ""].contains(text)
    if matches && (role == nil || str(e, kAXRoleAttribute) == role) {
      let r = AXUIElementPerformAction(e, kAXPressAction as CFString)
      print("press \(label(e)) -> \(r.rawValue)")
      done = true
      return true
    }
    return false
  }
  if !done { print("not found: \(text)"); exit(1) }
case "front":
  // Exit 0 only when the given pid is the frontmost app.
  let front = NSWorkspace.shared.frontmostApplication?.processIdentifier ?? -1
  print("front pid:", front)
  exit(front == pid ? 0 : 1)
case "activate":
  NSRunningApplication(processIdentifier: pid)?.activate(options: [.activateIgnoringOtherApps])
  Thread.sleep(forTimeInterval: 0.5)
case "winid":
  let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
  for w in list where (w[kCGWindowOwnerPID as String] as? pid_t) == pid && (w[kCGWindowLayer as String] as? Int) == 0 {
    print(w[kCGWindowNumber as String]!)
    break
  }
case "resize":
  // ax resize <pid> <count>: resizes the app's first window back and forth through AX (no mouse).
  let count = args.count > 3 ? Int(args[3])! : 50
  guard let win = (attr(app, kAXWindowsAttribute) as? [AXUIElement])?.first else { print("no window"); exit(1) }
  for i in 0..<count {
    var size = CGSize(width: 700 + (i % 7) * 37, height: 500 + (i % 5) * 41)
    let value = AXValueCreate(.cgSize, &size)!
    let t0 = Date()
    let r = AXUIElementSetAttributeValue(win, kAXSizeAttribute as CFString, value)
    let dt = Date().timeIntervalSince(t0)
    if dt > 0.5 { print("resize \(i) took \(dt) s") }
    if r != .success { print("resize \(i) failed \(r.rawValue) after \(dt) s"); exit(1) }
    Thread.sleep(forTimeInterval: Double(10 + (i * 17) % 60) / 1000.0)
  }
  print("resized \(count) times")
default: print("usage"); exit(2)
}
