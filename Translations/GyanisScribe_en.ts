<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US">
<context>
    <name>GyanisScribe</name>
        <message>
            <source>工具种类 0x{:X} 没有对应的工具：请把新工具登记进构造列表</source>
            <translation>Tool kind 0x{:X} has no tool behind it: register the new tool in the constructor list.</translation>
        </message>
        <message>
            <source>线宽必须是正数，收到的是 {}。请取 1 到 50 之间的整数</source>
            <translation>Line width must be a positive number; got {}. Use a whole number between 1 and 50.</translation>
        </message>
        <message>
            <source>字号必须是正数，收到的是 {}。请取 8 到 72 之间的整数</source>
            <translation>Font size must be a positive number; got {}. Use a whole number between 8 and 72.</translation>
        </message>
        <message>
            <source>当前工具种类没有对应的工具实现，请检查 setActiveTool 与构造列表</source>
            <translation>No tool implementation for this tool kind: check setActiveTool and the tool list in the constructor.</translation>
        </message>
        <message>
            <source>这一笔还没收：折线继续点下一个折点，双击收笔</source>
            <translation>This stroke is not finished yet: keep clicking to add polyline corners, double-click to finish</translation>
        </message>
        <message>
            <source>这一笔没有落定：尺寸太小或内容为空。请按住鼠标拖出可见的范围后再松手</source>
            <translation>This stroke didn't land: it's too small or empty. Hold the mouse button, drag out a visible area, then release</translation>
        </message>
        <message>
            <source>文字工具不在册</source>
            <translation>The text tool is not registered.</translation>
        </message>
        <message>
            <source>只有文字工具在用时才需要设定文本内容。请先切到文字工具再输入</source>
            <translation>Only the text tool needs text content. Switch to the text tool first, then type</translation>
        </message>
        <message>
            <source>现在没有在途的笔画可以收笔。折线至少要点出两个折点</source>
            <translation>There is no in-progress stroke to finish. A polyline needs at least two corners</translation>
        </message>
        <message>
            <source>序号工具不在册</source>
            <translation>The number tool is not registered.</translation>
        </message>
        <message>
            <source>要选中的图形不在册：工具 0x{:X} 索引 {}。请重新点选</source>
            <translation>The shape to select is not registered: tool 0x{:X}, index {}. Click it again.</translation>
        </message>
        <message>
            <source>没有选中任何图形，无法移动。请先点中要移动的那一条</source>
            <translation>Nothing is selected, so there is nothing to move. Click the shape you want to move first.</translation>
        </message>
        <message>
            <source>选中的图形已经不在了（多半刚被撤销或删除）。请重新点选</source>
            <translation>The selected shape is gone (it was probably just undone or deleted). Select it again.</translation>
        </message>
        <message>
            <source>没有选中任何图形，改不了文字。请先双击要改的那条文字</source>
            <translation>Nothing is selected, so there is no text to change. Double-click the text you want to edit</translation>
        </message>
        <message>
            <source>选中的不是文字图形，改不了内容。只有文字工具画出来的那几条可以双击重编</source>
            <translation>The selected shape is not text. Only shapes made with the Text tool can be re-edited by double-clicking</translation>
        </message>
        <message>
            <source>文字不能是空的：不要这一条就按 Delete 删掉，或按 Ctrl+Z 撤销这次改动</source>
            <translation>Text cannot be empty: press Delete to drop this shape, or Ctrl+Z to undo this change</translation>
        </message>
        <message>
            <source>没有选中任何图形，无需删除。请先点中要删的那一条</source>
            <translation>Nothing is selected, so there is nothing to delete. Click the shape you want to delete first.</translation>
        </message>
        <message>
            <source>选中的图形已经不在了。请重新点选</source>
            <translation>The selected shape is gone. Select it again.</translation>
        </message>
        <message>
            <source>没有可撤销的标注动作了</source>
            <translation>There is nothing left to undo.</translation>
        </message>
        <message>
            <source>没有可重做的标注动作了</source>
            <translation>There is nothing left to redo.</translation>
        </message>
        <message>
            <source>颜色 %1</source>
            <translation>Color %1</translation>
        </message>
        <message>
            <source>自定义颜色</source>
            <translation>Custom color</translation>
        </message>
        <message>
            <source>线宽</source>
            <translation>Stroke width</translation>
        </message>
        <message>
            <source>字号</source>
            <translation>Font size</translation>
        </message>
        <message>
            <source>填充</source>
            <translation>Fill</translation>
        </message>
        <message>
            <source>双向箭头</source>
            <translation>Double-headed arrow</translation>
        </message>
        <message>
            <source>撤销（Ctrl+Z）</source>
            <translation>Undo (Ctrl+Z)</translation>
        </message>
        <message>
            <source>重做（Ctrl+Y）</source>
            <translation>Redo (Ctrl+Y)</translation>
        </message>
        <message>
            <source>复制到剪贴板</source>
            <translation>Copy to clipboard</translation>
        </message>
        <message>
            <source>保存为文件</source>
            <translation>Save as file</translation>
        </message>
        <message>
            <source>贴到屏幕上</source>
            <translation>Pin to screen</translation>
        </message>
        <message>
            <source>识别文字</source>
            <translation>Recognize text</translation>
        </message>
        <message>
            <source>填充：无</source>
            <translation>Fill: none</translation>
        </message>
        <message>
            <source>填充：半透明</source>
            <translation>Fill: translucent</translation>
        </message>
        <message>
            <source>填充：实心</source>
            <translation>Fill: solid</translation>
        </message>
        <message>
            <source>双向箭头：开</source>
            <translation>Double-headed arrow: on</translation>
        </message>
        <message>
            <source>双向箭头：关</source>
            <translation>Double-headed arrow: off</translation>
        </message>
        <message>
            <source>选择标注颜色</source>
            <translation>Select annotation color</translation>
        </message>
        <message>
            <source>最近使用 %1</source>
            <translation>Recent %1</translation>
        </message>
        <message>
            <source>工具栏尺寸 {}x{} 不成立，摆放前要先有有效的 sizeHint</source>
            <translation>The toolbar size {}x{} doesn't hold: get a valid sizeHint before placing it.</translation>
        </message>
        <message>
            <source>选区 {}x{} 是空的，没有可参照的位置</source>
            <translation>The selection {}x{} is empty, so there is no position to line up with.</translation>
        </message>
        <message>
            <source>屏幕可用区 {}x{} 放不下任何东西，请确认目标屏幕还在</source>
            <translation>The screen's usable area {}x{} fits nothing: check that the target screen is still connected.</translation>
        </message>
        <message>
            <source>输入文字后回车</source>
            <translation>Type the text, then press Enter</translation>
        </message>
        <message>
            <source>箭头</source>
            <translation>Arrow</translation>
        </message>
        <message>
            <source>高斯模糊</source>
            <translation>Gaussian blur</translation>
        </message>
        <message>
            <source>椭圆</source>
            <translation>Ellipse</translation>
        </message>
        <message>
            <source>橡皮擦</source>
            <translation>Eraser</translation>
        </message>
        <message>
            <source>高亮</source>
            <translation>Highlighter</translation>
        </message>
        <message>
            <source>直线</source>
            <translation>Line</translation>
        </message>
        <message>
            <source>马赛克</source>
            <translation>Mosaic</translation>
        </message>
        <message>
            <source>序号</source>
            <translation>Number</translation>
        </message>
        <message>
            <source>画笔</source>
            <translation>Pen</translation>
        </message>
        <message>
            <source>折线</source>
            <translation>Polyline</translation>
        </message>
        <message>
            <source>矩形</source>
            <translation>Rectangle</translation>
        </message>
        <message>
            <source>文字</source>
            <translation>Text</translation>
        </message>
        <message>
            <source>截图未完成：%1。请重试一次</source>
            <translation>Capture did not complete: %1. Try again</translation>
        </message>
        <message>
            <source>这张贴图已移到当前分组，就摆在原处</source>
            <translation>This pin has been moved into the group on screen; it stays where it is</translation>
        </message>
        <message>
            <source>贴图已移到分组 {}，先收了起来；按 {} 或从托盘切到那一组就能看见</source>
            <translation>The pin was moved to group {}, so it is put away for now; press {} or switch to that group from the tray to see it</translation>
        </message>
        <message>
            <source>这些全局热键没能登记上：</source>
            <translation>These global hotkeys could not be registered:</translation>
        </message>
        <message>
            <source>上一次延时截图还在等，等它弹出来之后再按</source>
            <translation>The previous delayed capture is still waiting - press again once it comes up</translation>
        </message>
        <message>
            <source>{} 秒后开始截图：把要截的菜单或悬停状态摆好，这一会儿别动</source>
            <translation>Capture starts in {} seconds: get the menu or hover state ready and hold still for a moment</translation>
        </message>
        <message>
            <source>已让 {} 张贴图重新吃鼠标</source>
            <translation>Restored mouse input on {} pinned image(s)</translation>
        </message>
        <message>
            <source>没有贴图开着鼠标穿透</source>
            <translation>No pinned image is click-through right now</translation>
        </message>
        <message>
            <source>已摆回 {} 张贴图</source>
            <translation>Pinned {} image(s) back on the desk</translation>
        </message>
        <message>
            <source>已收起 {} 张贴图</source>
            <translation>Concealed {} image(s)</translation>
        </message>
        <message>
            <source>桌上没有贴图</source>
            <translation>No pinned images on the desk</translation>
        </message>
        <message>
            <source>已切到贴图分组 {}，桌上是这一组</source>
            <translation>Switched to pin group {}; that is the group on screen now</translation>
        </message>
        <message>
            <source>已复制到剪贴板</source>
            <translation>Copied to the clipboard</translation>
        </message>
        <message>
            <source>没能存下图片：{}</source>
            <translation>Couldn't save the image: {}</translation>
        </message>
        <message>
            <source>没能存下图片：%1</source>
            <translation>Could not save the image: %1</translation>
        </message>
        <message>
            <source>没能存下图片：这台机器的 Qt 写不出「{}」这一档。请在设置的截图页改选一档</source>
            <translation>Could not save the image: this Qt build cannot write the "{}" format. Pick another one on the Shot page of Settings</translation>
        </message>
        <message>
            <source>写入失败：{}。请检查这个目录能不能写</source>
            <translation>Write failed: {}. Check whether this directory is writable</translation>
        </message>
        <message>
            <source>已存成 {}</source>
            <translation>Saved as {}</translation>
        </message>
        <message>
            <source>已复制颜色 </source>
            <translation>Copied color </translation>
        </message>
        <message>
            <source>还没有可识别的图：先按 Ctrl+Alt+A 框选一块屏幕</source>
            <translation>No image to recognize yet: press Ctrl+Alt+A and drag out a screen region first</translation>
        </message>
        <message>
            <source>正在识别这一张…</source>
            <translation>Recognizing this image…</translation>
        </message>
        <message>
            <source>文字识别失败：{}。请重试一次，或换一种语言再试</source>
            <translation>Text recognition failed: {}. Try again, or switch to another language.</translation>
        </message>
        <message>
            <source>这台机器上没有可用的屏幕，取色没法开始</source>
            <translation>No usable screen on this machine; color picking cannot start</translation>
        </message>
        <message>
            <source>这一屏抓不下来：{}。请重试一次</source>
            <translation>This screen couldn't be captured: {}. Try again.</translation>
        </message>
        <message>
            <source>当前没有可用屏幕，录屏没法开始。请接回显示器后重试</source>
            <translation>No screen is available, so recording cannot start. Reconnect a display and try again.</translation>
        </message>
        <message>
            <source>还没有可录的选区：先按截图热键框一次选区，再开始录屏；也可以把录制范围改回整屏</source>
            <translation>No selection to record yet: frame one with the capture hotkey first, then start recording; you can also set the recording scope back to full screen</translation>
        </message>
        <message>
            <source>录屏没能开始：{}</source>
            <translation>Recording didn't start: {}</translation>
        </message>
        <message>
            <source>录屏收尾失败：{}</source>
            <translation>Finishing the recording failed: {}</translation>
        </message>
        <message>
            <source>正在编成品，好了会说一声</source>
            <translation>Building the finished file - you will hear when it is ready</translation>
        </message>
        <message>
            <source>控制条已收起；按 Ctrl+Alt+R 或托盘里的录屏项可停止并保存</source>
            <translation>The control bar is collapsed; press Ctrl+Alt+R or use the recording entry in the tray to stop and save</translation>
        </message>
        <message>
            <source>成品已出，但没带上声音：取声那一步中途失败了，原因写在日志里</source>
            <translation>The recording is done but has no sound: capturing audio failed partway through. The reason is in the log</translation>
        </message>
        <message>
            <source>成品已保存，但没进历史清单：{}</source>
            <translation>The file was saved but never reached the history list: {}</translation>
        </message>
        <message>
            <source>读不出这张图：{}。文件可能已被移动或删掉，或这个构建里没有对应的图片解码插件</source>
            <translation>This image can't be read: {}. The file may have been moved or deleted, or this build has no image decoder plugin for it.</translation>
        </message>
        <message>
            <source>识别图片文件</source>
            <translation>Recognize an image file</translation>
        </message>
        <message>
            <source>图片文件 (*.png *.jpg *.jpeg *.bmp *.webp *.gif)</source>
            <translation>Image files (*.png *.jpg *.jpeg *.bmp *.webp *.gif)</translation>
        </message>
        <message>
            <source>这条历史没删掉：{}</source>
            <translation>This history entry wasn't deleted: {}</translation>
        </message>
        <message>
            <source>已从历史里去掉，成品文件仍在 </source>
            <translation>Removed from history; the file is still at </translation>
        </message>
        <message>
            <source>打不开所在目录：{} 已经不在了。成品可能已被移动或删除</source>
            <translation>Can't open the folder: {} is gone. The artifact may have been moved or deleted</translation>
        </message>
        <message>
            <source>打不开目录 {}。请自己到设置里那个截图目录去找</source>
            <translation>Can't open {}. Look in the screenshot directory from Settings yourself</translation>
        </message>
        <message>
            <source>历史清单没清空：{}</source>
            <translation>The history list wasn't cleared: {}</translation>
        </message>
        <message>
            <source>历史列表已清空，成品文件仍在各自目录里</source>
            <translation>History list cleared; the artifact files are still in their own folders</translation>
        </message>
        <message>
            <source>录屏中止：{}。请重试一次</source>
            <translation>Recording aborted: {}. Try again.</translation>
        </message>
        <message>
            <source>取消录屏没成：{}</source>
            <translation>Couldn't cancel the recording: {}</translation>
        </message>
        <message>
            <source>已取消录屏：这一段没有保存，临时帧也删了</source>
            <translation>Recording cancelled: this take wasn't saved and its temporary frames are gone.</translation>
        </message>
        <message>
            <source>延时截图的秒数 "{}" 不在可用范围（0 到 {} 秒，0 表示不延时）里：请在设置的截图页重选，或删掉 Capture/DelaySeconds 这一项回到默认档</source>
            <translation>The capture delay "{}" is outside the usable range (0 to {} seconds, 0 means no delay): pick a value again on the Capture page in Settings, or delete the Capture/DelaySeconds entry to fall back to the default</translation>
        </message>
        <message>
            <source>进标注窗</source>
            <translation>Open in the annotation window</translation>
        </message>
        <message>
            <source>直接存成文件</source>
            <translation>Save straight to a file</translation>
        </message>
        <message>
            <source>直接贴到屏幕上</source>
            <translation>Pin straight onto the screen</translation>
        </message>
        <message>
            <source>直接识别文字</source>
            <translation>Recognise text right away</translation>
        </message>
        <message>
            <source>剪贴板里没有可以贴的内容：图像、纯文本或颜色都行。先复制一个再来贴</source>
            <translation>There is nothing on the clipboard to pin: an image, plain text, or a color all work. Copy something first</translation>
        </message>
        <message>
            <source>剪贴板里的图像读不出来（来源可能只挂了名目、没给像素）：换一张图复制，或先截图再来贴</source>
            <translation>The image on the clipboard cannot be read (the source may have offered only a label, not pixels): copy another image, or capture one and then paste</translation>
        </message>
        <message>
            <source>剪贴板里没有可以贴的内容：图像、纯文本或颜色都行。链接与文件这一类还没有转贴图的做法</source>
            <translation>There is nothing on the clipboard to pin: an image, plain text, or a color all work. Links and files have no pinning path yet</translation>
        </message>
        <message>
            <source>剪贴板里的文本是空的（只有空白）：先复制一段文字再来贴</source>
            <translation>The clipboard text is empty (whitespace only): copy some text first</translation>
        </message>
        <message>
            <source>剪贴板里的文本太长（{} 个字，上限 {}）：整张卡片会长到没法看，先把要贴的那一段截短再试</source>
            <translation>The clipboard text is too long ({} characters, limit {}): the card would be unreadably tall. Shorten the passage you want to pin and try again</translation>
        </message>
        <message>
            <source>智录遇到未处理的错误：{}。现场已尽量落到日志与崩溃转储，请到 {} 找最近一份日志（同目录下的 .dmp 是转储）；如果反复出现，请把日志、转储与出错的操作步骤一起反馈</source>
            <translation>GyanisScribe hit an unhandled error: {}. What could be saved went to the log and a crash dump; look for the newest log in {} (a .dmp next to it is the dump). If it keeps happening, send the log, the dump and the steps you took</translation>
        </message>
        <message>
            <source>智录：截图、标注、贴图、取色、录屏与文字识别</source>
            <translation>GyanisScribe: capture, annotate, pin, pick colour, record, and read text</translation>
        </message>
        <message>
            <source>把启动路径跑完就退出：热键登记与日志就绪算通过（退出码 0），否则 1</source>
            <translation>Run the startup path and exit: hotkey registration plus ready logging count as a pass (exit code 0), otherwise 1</translation>
        </message>
        <message>
            <source>这一类的说明</source>
            <translation>What this section covers</translation>
        </message>
        <message>
            <source>%1。留空表示用默认目录</source>
            <translation>%1. Leave empty to use the default folder</translation>
        </message>
        <message>
            <source>浏览…</source>
            <translation>Browse…</translation>
        </message>
        <message>
            <source>选择%1的目录</source>
            <translation>Choose the %1 folder</translation>
        </message>
        <message>
            <source>选择%1</source>
            <translation>Choose %1</translation>
        </message>
        <message>
            <source>不限</source>
            <translation>No limit</translation>
        </message>
        <message>
            <source>把光标位置圈在画面里</source>
            <translation>Ring the cursor position in the picture</translation>
        </message>
        <message>
            <source>光标高亮</source>
            <translation>Cursor highlight</translation>
        </message>
        <message>
            <source>整屏、选区与窗口录屏都生效；录窗口时按平台报出的客户区左上角落位，那份读数拿不到时这一帧就不画圈</source>
            <translation>Applies to full-screen, region, and window recording; when recording a window the ring is placed using the client-area top-left the platform reports, and a frame is left uncircled when that reading is unavailable</translation>
        </message>
        <message>
            <source>把鼠标指针画进成品</source>
            <translation>Draw the mouse pointer into the result</translation>
        </message>
        <message>
            <source>含鼠标指针</source>
            <translation>Include mouse pointer</translation>
        </message>
        <message>
            <source>整屏与区域截图生效；滚动长截图按帧接图，画进去会留下一串指针，那一趟不带。非 Windows 平台取不到系统指针的形状，开了也不会出现</source>
            <translation>Applies to full-screen and region shots. Scrolling capture stitches frames, so a pointer would repeat down the long image and is left out there. Non-Windows platforms cannot read the system pointer shape, so turning this on changes nothing</translation>
        </message>
        <message>
            <source>界面语言</source>
            <translation>Interface language</translation>
        </message>
        <message>
            <source>切换语言要重启智录才生效</source>
            <translation>Restart GyanisScribe for the language change to take effect</translation>
        </message>
        <message>
            <source>主题</source>
            <translation>Theme</translation>
        </message>
        <message>
            <source>识别语言</source>
            <translation>Recognition language</translation>
        </message>
        <message>
            <source>颜色写法</source>
            <translation>Color format</translation>
        </message>
        <message>
            <source>默认不透明度</source>
            <translation>Default opacity</translation>
        </message>
        <message>
            <source>贴图默认不透明度</source>
            <translation>Pin default opacity</translation>
        </message>
        <message>
            <source> 帧/秒</source>
            <translation> fps</translation>
        </message>
        <message>
            <source>默认帧率</source>
            <translation>Default frame rate</translation>
        </message>
        <message>
            <source>录屏默认帧率</source>
            <translation>Recording default frame rate</translation>
        </message>
        <message>
            <source> 秒</source>
            <translation> s</translation>
        </message>
        <message>
            <source>延时截图</source>
            <translation>Delayed capture</translation>
        </message>
        <message>
            <source>按下截图后等这么久再开始，用来截菜单、悬停提示这类一碰就没的画面；0 表示不延时</source>
            <translation>Wait this long after the key before capturing, for menus and hover hints that vanish as soon as you touch them; 0 means no delay</translation>
        </message>
        <message>
            <source> kbps</source>
            <translation> kbps</translation>
        </message>
        <message>
            <source>按质量优先</source>
            <translation>Quality first</translation>
        </message>
        <message>
            <source>画面码率</source>
            <translation>Video bitrate</translation>
        </message>
        <message>
            <source>录制范围</source>
            <translation>Recording scope</translation>
        </message>
        <message>
            <source>「上次选区」用最近一次截图框出的那块矩形；「当前窗口」录按下开始时最靠前的那个窗口。改完从下一次录屏开始生效</source>
            <translation>Last selection records the rectangle framed by the most recent capture; Current window records whichever window is in front when recording starts. Changes apply from the next recording</translation>
        </message>
        <message>
            <source>截完之后</source>
            <translation>After a capture</translation>
        </message>
        <message>
            <source>选「复制到剪贴板」「直接存成文件」「直接贴到屏幕上」时都不再弹标注窗；改完从下一次截图开始生效</source>
            <translation>Choosing "Copied to the clipboard", "Save straight to a file" or "Pin straight onto the screen" skips the annotation window; it takes effect from the next capture</translation>
        </message>
        <message>
            <source>保存格式</source>
            <translation>Save format</translation>
        </message>
        <message>
            <source>「直接存成文件」与贴图右键的「保存」按这一档写文件；PNG 无损，JPG 与 WebP 用来把照片类的截图压小</source>
            <translation>"Save directly to a file" and the pin context-menu "Save" write with this format. PNG is lossless; JPG and WebP shrink photo-like shots</translation>
        </message>
        <message>
            <source>保存质量</source>
            <translation>Save quality</translation>
        </message>
        <message>
            <source>只有 JPG 与 WebP 吃这一格，PNG 是无损的；数字越大越清晰，文件也越大</source>
            <translation>Only JPG and WebP use this value; PNG is lossless. Higher means clearer and bigger</translation>
        </message>
        <message>
            <source>不取这一路</source>
            <translation>Don't take this input</translation>
        </message>
        <message>
            <source>输出格式</source>
            <translation>Output format</translation>
        </message>
        <message>
            <source>GIF 走两遍调色板，帧率上限只有 %1 帧/秒，比 MP4 低；MP4 适合长片段。改完从下一次录屏开始生效</source>
            <translation>GIF runs a two-pass palette and caps the frame rate at %1 frames per second, lower than MP4; MP4 suits longer clips. Changes apply from the next recording</translation>
        </message>
        <message>
            <source>智录 GyanisScribe %1\n\n截图、标注、贴图、录屏与文字识别。\n全局热键：Ctrl+Alt+A 截图，Ctrl+Alt+R 录屏（再按一次停止并保存），Ctrl+Alt+O 识别。\n界面图标全部为本项目原创 SVG；第三方组件与其许可证见仓库 docs/ThirdPartyLicenses.md。\n</source>
            <translation>GyanisScribe %1\n\nScreenshot, annotation, pin, screen recording and text recognition.\nGlobal hotkeys: Ctrl+Alt+A capture, Ctrl+Alt+R record (press again to stop and save), Ctrl+Alt+O recognize.\nAll UI icons are original SVG artwork made for this project; third-party components and their licenses are listed in docs/ThirdPartyLicenses.md in the repository.\n</translation>
        </message>
        <message>
            <source>智录 设置</source>
            <translation>GyanisScribe Settings</translation>
        </message>
        <message>
            <source>设置导航</source>
            <translation>Settings navigation</translation>
        </message>
        <message>
            <source>已导出 %1 项设置</source>
            <translation>Exported %1 settings</translation>
        </message>
        <message>
            <source>已导入 %1 项设置。这一页显示的还是导入前的值，请关掉设置窗口再重新打开；界面语言要重启智录才生效。</source>
            <translation>Imported %1 settings. This page still shows the values from before the import: close the settings window and open it again. The interface language takes effect only after restarting GyanisScribe.</translation>
        </message>
        <message>
            <source>导出配置…</source>
            <translation>Export settings…</translation>
        </message>
        <message>
            <source>导出配置</source>
            <translation>Export settings</translation>
        </message>
        <message>
            <source>把当前全部设置写成一个 JSON 文件，换机器或备份时用</source>
            <translation>Writes all current settings into one JSON file, for a backup or another machine</translation>
        </message>
        <message>
            <source>导入配置…</source>
            <translation>Import settings…</translation>
        </message>
        <message>
            <source>导入配置</source>
            <translation>Import settings</translation>
        </message>
        <message>
            <source>从本程序导出的 JSON 文件读回设置；订阅配置的热键与主题立刻跟上新值</source>
            <translation>Reads settings back from a file this program exported; hotkeys and theme, which watch the configuration, follow the new values right away</translation>
        </message>
        <message>
            <source>配置文件 (*.json)</source>
            <translation>Configuration files (*.json)</translation>
        </message>
        <message>
            <source>与托盘菜单、全局热键走同一个出口</source>
            <translation>Same action as the tray menu and the global hotkey</translation>
        </message>
        <message>
            <source>更多</source>
            <translation>More</translation>
        </message>
        <message>
            <source>开机时自动启动</source>
            <translation>Start automatically at sign-in</translation>
        </message>
        <message>
            <source>开机时自动起这一份程序；状态读的是系统里的启动项，改完立即生效</source>
            <translation>Start this copy of the program at sign-in; the state is read from the system's startup items and takes effect immediately</translation>
        </message>
        <message>
            <source>界面语言决定菜单、托盘提示与这些设置页的措辞，切换后要重启智录才生效。下面两格管历史留多少，填到 0 就是不限。</source>
            <translation>The interface language sets the wording of the menus, the tray hints and these settings pages; a change takes effect after restarting GyanisScribe. The two rows below decide how much history is kept; 0 means no limit.</translation>
        </message>
        <message>
            <source>历史最多留多少条</source>
            <translation>Maximum history entries</translation>
        </message>
        <message>
            <source> 条</source>
            <translation> entries</translation>
        </message>
        <message>
            <source>历史最久留多少天</source>
            <translation>How long history is kept, in days</translation>
        </message>
        <message>
            <source> 天</source>
            <translation> days</translation>
        </message>
        <message>
            <source>开机自启动</source>
            <translation>Start at sign-in</translation>
        </message>
        <message>
            <source>备份与恢复</source>
            <translation>Backup and restore</translation>
        </message>
        <message>
            <source>文件名模板</source>
            <translation>File name template</translation>
        </message>
        <message>
            <source>可用变量：%date%（日期）、%time%（时刻）、%ms%（毫秒）；留空表示用默认档。改动后从下一次保存起生效</source>
            <translation>Available variables: %date% (date), %time% (time), %ms% (milliseconds); leave blank to use the default. Changes take effect from the next save</translation>
        </message>
        <message>
            <source>截图与标注成品落在这个目录里。取色复制出来的文本按下面的写法格式化。</source>
            <translation>Screenshots and finished annotations land in this folder. Copied colors are formatted with the format chosen below.</translation>
        </message>
        <message>
            <source>鼠标指针</source>
            <translation>Mouse pointer</translation>
        </message>
        <message>
            <source>截图与标注成品目录</source>
            <translation>Folder for screenshots and annotations</translation>
        </message>
        <message>
            <source>截图目录</source>
            <translation>Screenshots</translation>
        </message>
        <message>
            <source>复制用的写法</source>
            <translation>Format used when copying</translation>
        </message>
        <message>
            <source>新打开的标注窗按这两档起步。画过之后的改动会被工具栏记下来，下次仍以最近用的那一档开始。</source>
            <translation>A newly opened annotation window starts from these two steps. Once you have drawn, the toolbar remembers the step you actually used and starts from that next time.</translation>
        </message>
        <message>
            <source>默认线宽</source>
            <translation>Default stroke width</translation>
        </message>
        <message>
            <source>默认字号</source>
            <translation>Default font size</translation>
        </message>
        <message>
            <source>新贴到屏幕上的图按这个不透明度开始。滚轮调透明度只影响那一张贴图，不改这里。</source>
            <translation>A newly pinned image starts at this opacity. Scrolling to change opacity affects only that pin, not this setting.</translation>
        </message>
        <message>
            <source>新建贴图的默认不透明度</source>
            <translation>Opacity for new pins</translation>
        </message>
        <message>
            <source>录屏成品落在这个目录里。录制范围选「上次选区」或「当前窗口」时，画面里就只有那一片。存成 GIF 时帧率上限比 MP4 低。画面码率填 0 就交给编码器的质量档，给了具体数值就按那个码率控，GIF 用不上这一项。取声那两格列不出设备时只剩「不取这一路」，把鼠标停在框上能看到原因；存成 GIF 时这两格会灰掉，那种文件带不了声音。这几项都从下一次开始录屏时生效。</source>
            <translation>Recorded files land in this folder. With Last selection or Current window the picture keeps only that part of the screen. GIF allows a lower frame rate ceiling than MP4. A video bitrate of 0 leaves the quality to the encoder, a concrete number caps the bitrate, and GIF ignores this row. When the two audio rows can't list any device, only "Don't take this input" is left there - hover over the box to read why; with GIF those two rows grey out because that container carries no sound. All of these apply from the next recording.</translation>
        </message>
        <message>
            <source>录屏成品目录</source>
            <translation>Folder for recordings</translation>
        </message>
        <message>
            <source>录屏目录</source>
            <translation>Recordings</translation>
        </message>
        <message>
            <source>麦克风设备</source>
            <translation>Microphone device</translation>
        </message>
        <message>
            <source>系统声音设备</source>
            <translation>System sound device</translation>
        </message>
        <message>
            <source>麦克风这一路</source>
            <translation>Microphone input</translation>
        </message>
        <message>
            <source>系统声音这一路</source>
            <translation>System sound input</translation>
        </message>
        <message>
            <source>认得出哪种文字取决于语言包。目录留空时按环境变量与程序数据目录找。</source>
            <translation>Which scripts can be recognized depends on the language packs. Leave the folder empty to fall back to the environment variable and the program data folder.</translation>
        </message>
        <message>
            <source>识别语言包目录</source>
            <translation>Recognition language pack folder</translation>
        </message>
        <message>
            <source>语言包目录</source>
            <translation>Language packs</translation>
        </message>
        <message>
            <source>截图</source>
            <translation>Screenshot</translation>
        </message>
        <message>
            <source>录屏</source>
            <translation>Screen recording</translation>
        </message>
        <message>
            <source>文字识别</source>
            <translation>Text recognition</translation>
        </message>
        <message>
            <source>屏幕取色</source>
            <translation>Screen color picker</translation>
        </message>
        <message>
            <source>滚动长截图</source>
            <translation>Scrolling capture</translation>
        </message>
        <message>
            <source>收起 / 摆回贴图</source>
            <translation>Conceal / restore pins</translation>
        </message>
        <message>
            <source>切换贴图分组</source>
            <translation>Switch pin group</translation>
        </message>
        <message>
            <source>%1 个全局热键随时可改，改完立即重新登记，不需要重启。</source>
            <translation>%1 global hotkeys can be changed any time; each change is re-registered right away, no restart needed.</translation>
        </message>
        <message>
            <source>这个平台没有登记全局热键的接口，下面 %1 项改了也不会生效。</source>
            <translation>This platform has no way to register global hotkeys, so changing the %1 entries below has no effect.</translation>
        </message>
        <message>
            <source>%1 的全局热键</source>
            <translation>Global hotkey for %1</translation>
        </message>
        <message>
            <source>配色方案即时生效。高对比度档是纯黑正文、纯白底与加粗边框。</source>
            <translation>Color schemes apply immediately. High contrast uses pure black text on pure white with bold borders.</translation>
        </message>
        <message>
            <source>配色方案</source>
            <translation>Color scheme</translation>
        </message>
        <message>
            <source>智录图标</source>
            <translation>GyanisScribe icon</translation>
        </message>
        <message>
            <source>检查更新</source>
            <translation>Check for updates</translation>
        </message>
        <message>
            <source>正在检查…</source>
            <translation>Checking…</translation>
        </message>
        <message>
            <source>通用</source>
            <translation>General</translation>
        </message>
        <message>
            <source>标注</source>
            <translation>Annotation</translation>
        </message>
        <message>
            <source>贴图</source>
            <translation>Pin</translation>
        </message>
        <message>
            <source>快捷键</source>
            <translation>Keyboard shortcuts</translation>
        </message>
        <message>
            <source>关于</source>
            <translation>About</translation>
        </message>
        <message>
            <source>历史</source>
            <translation>History</translation>
        </message>
        <message>
            <source>浅色</source>
            <translation>Light</translation>
        </message>
        <message>
            <source>深色</source>
            <translation>Dark</translation>
        </message>
        <message>
            <source>跟随系统</source>
            <translation>Follow system</translation>
        </message>
        <message>
            <source>高对比度</source>
            <translation>High contrast</translation>
        </message>
        <message>
            <source>中文</source>
            <translation>中文</translation>
        </message>
        <message>
            <source>智录 · 录制已暂停 %1</source>
            <translation>GyanisScribe · recording paused at %1</translation>
        </message>
        <message>
            <source>智录 · 正在录制 %1</source>
            <translation>GyanisScribe · recording %1</translation>
        </message>
        <message>
            <source>智录</source>
            <translation>GyanisScribe</translation>
        </message>
        <message>
            <source>智录 · 贴图 %1 张</source>
            <translation>GyanisScribe · %1 pin(s)</translation>
        </message>
        <message>
            <source>框选截图</source>
            <translation>Capture a region</translation>
        </message>
        <message>
            <source>整屏截图</source>
            <translation>Capture the full screen</translation>
        </message>
        <message>
            <source>重复上次选区</source>
            <translation>Repeat last selection</translation>
        </message>
        <message>
            <source>截取当前窗口</source>
            <translation>Capture the foreground window</translation>
        </message>
        <message>
            <source>开始 / 停止录屏</source>
            <translation>Start / stop recording</translation>
        </message>
        <message>
            <source>识别图片文件…</source>
            <translation>Recognize an image file…</translation>
        </message>
        <message>
            <source>关闭全部贴图</source>
            <translation>Close all pins</translation>
        </message>
        <message>
            <source>收起 / 摆回全部贴图</source>
            <translation>Conceal / restore all pinned images</translation>
        </message>
        <message>
            <source>取消全部贴图穿透</source>
            <translation>Let every pinned image receive the mouse again</translation>
        </message>
        <message>
            <source>把剪贴板里的图贴到屏幕上</source>
            <translation>Paste the clipboard image onto the screen</translation>
        </message>
        <message>
            <source>切到下一组贴图</source>
            <translation>Switch to the next pin group</translation>
        </message>
        <message>
            <source>设置</source>
            <translation>Settings</translation>
        </message>
        <message>
            <source>关于智录</source>
            <translation>About GyanisScribe</translation>
        </message>
        <message>
            <source>退出</source>
            <translation>Quit</translation>
        </message>
        <message>
            <source>快捷键：%1，可在设置页改</source>
            <translation>Shortcut: %1. Change it on the Settings page</translation>
        </message>
        <message>
            <source>超时</source>
            <translation>timed out</translation>
        </message>
        <message>
            <source>域名解析不了</source>
            <translation>the host name cannot be resolved</translation>
        </message>
        <message>
            <source>连接被拒绝</source>
            <translation>the connection was refused</translation>
        </message>
        <message>
            <source>连接中断</source>
            <translation>the connection dropped</translation>
        </message>
        <message>
            <source>网络错误</source>
            <translation>network error</translation>
        </message>
        <message>
            <source>更新检查的回应不是一份发布信息（不是 JSON 对象）：多半是网络中间被拦了，请稍后再试</source>
            <translation>The update response is not a release description (not a JSON object): something in the middle of the network probably intercepted it, please try again later</translation>
        </message>
        <message>
            <source>发布信息里没有版本标签：请到发布页看有没有新版</source>
            <translation>The release has no version tag: check the releases page for a new version</translation>
        </message>
        <message>
            <source>发布信息里没有发布页地址：请到发布页看有没有新版</source>
            <translation>The release has no page address: check the releases page for a new version</translation>
        </message>
        <message>
            <source>查不到发布信息（404）：这个仓库还没公开，或还没有发布过正式版。请到发布页自己看一眼</source>
            <translation>No release information found (404): this repository is not public yet, or has no published release yet. Take a look at the releases page yourself</translation>
        </message>
        <message>
            <source>GitHub 拒绝了这次请求（HTTP {}）：多半是请求太频繁，也可能网络中间被拦。过一会儿再试，或直接去发布页看</source>
            <translation>GitHub rejected this request (HTTP {}): probably too many requests, or something in the middle of the network intercepted it. Try again in a while, or go straight to the releases page</translation>
        </message>
        <message>
            <source>连不上 GitHub（{}）：检查网络或代理后重试，或直接去发布页看</source>
            <translation>Cannot reach GitHub ({}): check your network or proxy and retry, or go straight to the releases page</translation>
        </message>
        <message>
            <source>检查更新失败（{}）：请稍后再试，或直接去发布页看</source>
            <translation>Update check failed ({}): try again later, or go straight to the releases page</translation>
        </message>
        <message>
            <source>本机版本号「{}」读不出来，没法与最新版比。请到发布页自己看一眼</source>
            <translation>The local version "{}" cannot be parsed, so it cannot be compared with the latest one. Take a look at the releases page yourself</translation>
        </message>
        <message>
            <source>发布页上的版本标签「{}」读不出来，没法与本机版本比</source>
            <translation>The version tag "{}" on the releases page cannot be parsed, so it cannot be compared with the local version</translation>
        </message>
        <message>
            <source>有新版本 {}（本机 {}）：点这一行打开下载页</source>
            <translation>A new version {} is available (you have {}): click this line to open the download page</translation>
        </message>
        <message>
            <source>已是最新（{}）</source>
            <translation>Up to date ({})</translation>
        </message>
        <message>
            <source>检查更新失败：{}。请稍后再试，或到发布页自己看一眼</source>
            <translation>Update check failed: {}. Try again later, or take a look at the releases page yourself</translation>
        </message>
        <message>
            <source>已复制尺寸 %1</source>
            <translation>Copied size %1</translation>
        </message>
        <message>
            <source>还没有可复用的上次区域：先框选截图一次，或改用区域截图</source>
            <translation>No previous region to reuse yet: drag out one capture first, or use region capture.</translation>
        </message>
        <message>
            <source>滚动长截图还在接：先在控制条上按完成或取消，再开始下一次截图</source>
            <translation>A scrolling capture is still running: press Done or Cancel on its control bar before starting another one.</translation>
        </message>
        <message>
            <source>进入框选失败：Qt 没有报出任何屏幕。请在 GUI 会话里重试</source>
            <translation>Cannot start region capture: Qt reported no screens. Try again inside a GUI session.</translation>
        </message>
        <message>
            <source>已接 {} 像素高：这一段和上一段一样，往下滚一点再接</source>
            <translation>Stitched to {} px: this stretch matches the previous one, scroll a bit further</translation>
        </message>
        <message>
            <source>已接 {} 像素高</source>
            <translation>Stitched to {} px</translation>
        </message>
        <message>
            <source>DXGI 桌面重复认不出屏幕（{}×{}，原点 {}）：Qt 报的几何与适配器的输出坐标对不上。请重试一次，或改用 Qt 的抓帧原语</source>
            <translation>DXGI desktop duplication cannot match a screen ({}x{}, origin {}): the geometry Qt reports does not match any adapter output. Try again, or switch to the Qt capture primitive</translation>
        </message>
        <message>
            <source>这块屏所挂的显示适配器建不起 D3D11 设备（{}）：请改用 Qt 的抓帧原语</source>
            <translation>The display adapter behind this screen cannot create a D3D11 device ({}): switch to the Qt capture primitive</translation>
        </message>
        <message>
            <source>这块屏开不了 DXGI 桌面重复（{}）：远程会话、驱动或桌面格式可能不支持。请改用 Qt 的抓帧原语</source>
            <translation>This screen cannot open DXGI desktop duplication ({}): a remote session, the driver, or the desktop format may not support it. Switch to the Qt capture primitive</translation>
        </message>
        <message>
            <source>DXGI 抓帧连续两轮失效（{}）：画面刚切过分辨率或会话被锁。请重试一次</source>
            <translation>DXGI capture failed twice in a row ({}): the display just changed resolution or the session is locked. Try again</translation>
        </message>
        <message>
            <source>DXGI 桌面重复开了之后一直没等到第一帧：画面可能刚被锁屏或切走。请重试一次</source>
            <translation>DXGI desktop duplication never delivered a first frame: the screen may have just been locked or switched away. Try again</translation>
        </message>
        <message>
            <source>DXGI 桌面重复已失效（切分辨率或会话切换）：接口要重开一轮</source>
            <translation>DXGI desktop duplication went stale (resolution change or session switch): the interface has to be reopened</translation>
        </message>
        <message>
            <source>DXGI 取帧失败（HRESULT 0x{:08X}）</source>
            <translation>DXGI failed to acquire a frame (HRESULT 0x{:08X})</translation>
        </message>
        <message>
            <source>DXGI 交回的帧不是 2D 纹理：这一版取帧路径与驱动对不上</source>
            <translation>The frame DXGI returned is not a 2D texture: this capture path does not match the driver</translation>
        </message>
        <message>
            <source>桌面的像素格式是 0x{:X}，这一路只认 B8G8R8A8：请改用 Qt 的抓帧原语</source>
            <translation>The desktop pixel format is 0x{:X}; this path only handles B8G8R8A8. Switch to the Qt capture primitive</translation>
        </message>
        <message>
            <source>DXGI 抓帧：建暂存纹理失败。请重试一次；一直失败请改用 Qt 的抓帧原语</source>
            <translation>DXGI capture: creating the staging texture failed. Try again; if it keeps failing, switch to the Qt capture primitive</translation>
        </message>
        <message>
            <source>DXGI 抓帧：暂存纹理读不出来。请重试一次</source>
            <translation>DXGI capture: the staging texture cannot be read. Try again</translation>
        </message>
        <message>
            <source>DXGI 抓帧：读出来的是空图。请重试一次</source>
            <translation>DXGI capture: the read came back as an empty image. Try again</translation>
        </message>
        <message>
            <source>找不到名为「{}」的屏幕：多半已经被拔掉。请在不填屏幕名的情况下重抓一次（那样按光标所在屏走）</source>
            <translation>Cannot find a screen named "{}": it was probably unplugged. Capture again with no screen name, which follows the screen under the cursor.</translation>
        </message>
        <message>
            <source>Qt 没有报出任何屏幕：请在 QGuiApplication 建好之后再抓图</source>
            <translation>Qt reported no screens: capture only after QGuiApplication has been created.</translation>
        </message>
        <message>
            <source>长截图接不上：这一帧是空的。请重新截一次</source>
            <translation>Scrolling capture cannot continue: this frame is empty. Capture it again</translation>
        </message>
        <message>
            <source>长截图接不上：这一帧的宽度和前面不一致（多半是中途换了屏或改了缩放）。先把已有这一段收好再重来</source>
            <translation>Scrolling capture cannot continue: this frame is a different width than the earlier ones (the screen or the scaling changed mid-way). Finish what you already have, then start over</translation>
        </message>
        <message>
            <source>长截图控制条</source>
            <translation>Scrolling capture bar</translation>
        </message>
        <message>
            <source>滚动要截的页面，接好后按完成</source>
            <translation>Scroll the page you are capturing, then press Done</translation>
        </message>
        <message>
            <source>长截图提示</source>
            <translation>Scrolling capture hint</translation>
        </message>
        <message>
            <source>还没接上任何一段</source>
            <translation>Nothing stitched yet</translation>
        </message>
        <message>
            <source>已接高度</source>
            <translation>Stitched height</translation>
        </message>
        <message>
            <source>取消</source>
            <translation>Cancel</translation>
        </message>
        <message>
            <source>扔掉这一段，不产出长图</source>
            <translation>Discard this run without producing an image</translation>
        </message>
        <message>
            <source>取消长截图</source>
            <translation>Cancel scrolling capture</translation>
        </message>
        <message>
            <source>完成</source>
            <translation>Done</translation>
        </message>
        <message>
            <source>结束拼接，把这张长图交给下一步</source>
            <translation>Stop stitching and hand this tall image to the next step</translation>
        </message>
        <message>
            <source>完成长截图</source>
            <translation>Finish scrolling capture</translation>
        </message>
        <message>
            <source>抓「{}」这块屏拿到了空图：本平台可能不允许直接抓取屏幕。请检查会话类型（远程会话、Wayland 沙箱）后重试</source>
            <translation>Capturing screen "{}" returned an empty image: this platform may not allow grabbing the screen directly. Check the session type (remote session, Wayland sandbox) and retry.</translation>
        </message>
        <message>
            <source>抓窗口失败：传进来的窗口句柄是空的。请确认窗口还没有被关闭，或改用区域截图</source>
            <translation>Window capture failed: the window handle is empty. Make sure the window is still open, or use region capture.</translation>
        </message>
        <message>
            <source>本平台抓不出窗口像素（句柄 {}）。请改用区域截图框住那个窗口</source>
            <translation>This platform can't grab window pixels (handle {}). Use region capture and frame that window instead.</translation>
        </message>
        <message>
            <source>抓不到句柄 {} 对应的窗口：本平台不允许抓取他人窗口，或窗口已关闭。请改用区域截图</source>
            <translation>The window for handle {} can't be captured: this platform doesn't allow grabbing other programs' windows, or the window is already closed. Use region capture instead.</translation>
        </message>
        <message>
            <source>缩放因子必须大于 0，收到的是 {}。请检查这一帧来自哪块屏幕，dpr 不该由调用方自己猜</source>
            <translation>The scale factor must be greater than 0; got {}. Check which screen this frame came from: the caller must not guess the dpr.</translation>
        </message>
        <message>
            <source>选区超出了这一帧：帧是 {}×{}，换算后要到 {}。请确认这块屏幕有没有被拔掉，或缩放因子有没有变</source>
            <translation>The selection runs outside this frame: the frame is {}x{}, the converted area reaches {}. Check whether this screen was unplugged or the scale factor changed.</translation>
        </message>
        <message>
            <source>上次选区的记录格式不对：收到「{}」，应为「x,y,width,height」四个整数。请删掉这一项，让程序重新记一次</source>
            <translation>The last-selection record has the wrong format: got "{}", expected four integers "x,y,width,height". Delete this entry and let the program record it again.</translation>
        </message>
        <message>
            <source>上次选区的记录第 {} 段「{}」不是整数。请删掉这一项，让程序重新记一次</source>
            <translation>Part {} of the last-selection record, "{}", is not a whole number. Delete this entry and let the program record it again.</translation>
        </message>
        <message>
            <source>上次选区的宽高必须是正数，收到的是 {}×{}。请删掉这一项，让程序重新记一次</source>
            <translation>The last selection's width and height must be positive; got {}x{}. Delete this entry and let the program record it again.</translation>
        </message>
        <message>
            <source>{}：取值 "{}" 不在可选值里，可取的只有 {}。请在设置的{}重选，或删掉 {} 这一项回到默认档</source>
            <translation>{}: the value "{}" is not among the allowed ones, which are only {}. Re-pick it on the {} page in Settings, or delete the {} entry to fall back to the default</translation>
        </message>
        <message>
            <source>截图页</source>
            <translation>Shot page</translation>
        </message>
        <message>
            <source>保存质量：取值 {} 不在可取区间里，只能是 {} 到 {}。请在设置的截图页重选，或删掉 Save/ImageQuality 这一项回到默认档</source>
            <translation>Save quality: value {} is out of range, only {} to {} are accepted. Choose again on the Shot page of Settings, or delete the Save/ImageQuality entry to fall back to the default</translation>
        </message>
        <message>
            <source>PNG（无损）</source>
            <translation>PNG (lossless)</translation>
        </message>
        <message>
            <source>日志目录建不出来：{}（原因 {}）。请把 Paths 里的日志目录改到一个可写的路径再重启</source>
            <translation>The log directory can't be created: {} (reason {}). Point the log directory in Paths at a writable path, then restart.</translation>
        </message>
        <message>
            <source>日志文件打不开：{}（原因 {}）。请关闭占用它的程序，或换一个日志目录</source>
            <translation>The log file can't be opened: {} (reason {}). Close the program holding it, or pick another log directory.</translation>
        </message>
        <message>
            <source>目录创建失败：{}（原因 {}）。请检查该路径是否可写，或在设置里换一个目录</source>
            <translation>The directory can't be created: {} (reason {}). Check that this path is writable, or pick another directory in Settings.</translation>
        </message>
        <message>
            <source>路径存在但不是目录</source>
            <translation>The path exists but is not a directory</translation>
        </message>
        <message>
            <source>文件名模板不能为空：可以填 %date%_%time%，或留空用默认档</source>
            <translation>The file name template cannot be empty: fill in something like %date%_%time%, or leave it blank for the default</translation>
        </message>
        <message>
            <source>文件名模板太长（{} 个字，上限 {}）：太长的名字在别的程序里也常常打不开</source>
            <translation>The file name template is too long ({} characters, limit {}): names that long often fail to open in other programs</translation>
        </message>
        <message>
            <source>文件名模板里不能有「{}」：它会把文件写到别的目录，或让系统直接拒绝写盘</source>
            <translation>The file name template cannot contain "{}": it would put the file in another folder, or make the system refuse the write</translation>
        </message>
        <message>
            <source>文件名模板里的变量「%{}%」不认得：可用的只有 %date%（日期）、%time%（时刻）、%ms%（毫秒）</source>
            <translation>Unknown variable "%{}%" in the file name template: the only ones are %date% (date), %time% (time) and %ms% (milliseconds)</translation>
        </message>
        <message>
            <source>…</source>
            <translation>…</translation>
        </message>
        <message>
            <source>历史清单里没有这条记录：{}。请刷新列表后再试一次</source>
            <translation>The history list has no such record: {}. Refresh the list and try again.</translation>
        </message>
        <message>
            <source>历史清单打不开：{}。请检查这个文件是否被别的程序占着，或把它删掉让软件重建</source>
            <translation>The history list can't be opened: {}. Check whether another program holds this file, or delete it and let the app rebuild it.</translation>
        </message>
        <message>
            <source>历史清单格式不对：{}。可以把这个文件删掉让软件重建（成品文件不受影响），或按 {{"entries": [...]}} 的形状修好</source>
            <translation>The history list is malformed: {}. Delete this file and let the app rebuild it (the saved files are not touched), or fix it into the shape {{"entries": [...]}}.</translation>
        </message>
        <message>
            <source>历史清单第 {} 条缺 path、kind 或 moment。请修好这一条，或删掉整个文件让软件重建：{}</source>
            <translation>History entry {} lacks path, kind or moment: {}. Fix this entry, or delete the whole file and let the app rebuild it.</translation>
        </message>
        <message>
            <source>历史清单第 {} 条：{}</source>
            <translation>History entry {}: {}</translation>
        </message>
        <message>
            <source>历史清单第 {} 条的时间 "{}" 读不出来。请用 2026-10-10T08:30:00.000 这种写法</source>
            <translation>The time of history entry {} reads as "{}", which can't be parsed. Use a form like 2026-10-10T08:30:00.000.</translation>
        </message>
        <message>
            <source>历史清单写不了：{}。请确认这个目录还能写，或先把软件设的截图目录改到一个可写位置</source>
            <translation>The history list can't be written: {}. Check that this directory is still writable, or move the screenshot folder to a writable place first.</translation>
        </message>
        <message>
            <source>历史清单没能落盘：{}。请重试一次，或换一个可写的目录</source>
            <translation>The history list wasn't saved to disk: {}. Try again, or pick a writable directory.</translation>
        </message>
        <message>
            <source>历史文件里的类别 "{}" 不认识。可用取值只有 Screenshot 与 Recording（区分大小写），改不回任一取值就把这一行删掉</source>
            <translation>The category "{}" in the history file is unknown. Only Screenshot and Recording are valid (case-sensitive); drop this line if it can't be set to one of them.</translation>
        </message>
        <message>
            <source>今天 </source>
            <translation>Today </translation>
        </message>
        <message>
            <source>昨天 </source>
            <translation>Yesterday </translation>
        </message>
        <message>
            <source>全部</source>
            <translation>All</translation>
        </message>
        <message>
            <source>按文件名、类别或日期搜索</source>
            <translation>Search by file name, category or date</translation>
        </message>
        <message>
            <source>历史搜索</source>
            <translation>History search</translation>
        </message>
        <message>
            <source>历史类别</source>
            <translation>History category</translation>
        </message>
        <message>
            <source>重新贴图</source>
            <translation>Re-pin</translation>
        </message>
        <message>
            <source>重新识别</source>
            <translation>Recognize again</translation>
        </message>
        <message>
            <source>打开所在目录</source>
            <translation>Open containing folder</translation>
        </message>
        <message>
            <source>删除</source>
            <translation>Delete</translation>
        </message>
        <message>
            <source>清空列表</source>
            <translation>Clear list</translation>
        </message>
        <message>
            <source>历史列表</source>
            <translation>History list</translation>
        </message>
        <message>
            <source>先在历史里选一张图</source>
            <translation>Select an image in the history first</translation>
        </message>
        <message>
            <source>历史列表本来就是空的</source>
            <translation>The history list is already empty</translation>
        </message>
        <message>
            <source>清空历史列表</source>
            <translation>Clear the history list</translation>
        </message>
        <message>
            <source>要把历史列表里的记录全部去掉吗？成品文件本身不会被删除，仍在各自目录里。</source>
            <translation>Remove every entry from the history list? The artifact files themselves are not deleted and stay in their folders.</translation>
        </message>
        <message>
            <source>历史清单没能读出来：{}。列表先显示上一次的记录</source>
            <translation>The history list couldn't be read: {}. The list keeps showing the previous records.</translation>
        </message>
        <message>
            <source>（删除只从历史里去掉这条，成品文件留在原处）</source>
            <translation>(Delete only removes this entry from the history; the file itself stays where it is)</translation>
        </message>
        <message>
            <source>组合键「{}」里有一段是空的（相邻的 + 之间没有内容）。请写成 Ctrl+Alt+A 这种每段都有内容的形式</source>
            <translation>The shortcut "{}" has an empty part (nothing between two neighbouring +). Write it with content in every part, like Ctrl+Alt+A.</translation>
        </message>
        <message>
            <source>组合键「{}」里的「{}」不是修饰键。修饰键只认 Ctrl、Alt、Shift、Win（Control/Option/Meta/Super 同义）</source>
            <translation>The shortcut "{}" contains "{}", which is not a modifier. Only Ctrl, Alt, Shift and Win count (Control/Option/Meta/Super mean the same).</translation>
        </message>
        <message>
            <source>组合键「{}」最后没有按键。请写成 Ctrl+Alt+A 这种「修饰键+按键」的形式</source>
            <translation>The shortcut "{}" ends without a key. Write it as modifiers plus a key, like Ctrl+Alt+A.</translation>
        </message>
        <message>
            <source>组合键「{}」里的按键「{}」不支持。请改用字母 A-Z、数字 0-9、Space 或 F1-F24</source>
            <translation>The shortcut "{}" uses the key "{}", which is not supported. Use a letter A-Z, a digit 0-9, Space or F1-F24.</translation>
        </message>
        <message>
            <source>组合键的按键部分不能是修饰键本身：请在 Ctrl、Alt、Shift、Win 之外再选一个字母、数字、空格或 F1-F24</source>
            <translation>The key part of a shortcut cannot be a modifier itself: pick a letter, digit, Space or F1-F24 besides Ctrl, Alt, Shift and Win.</translation>
        </message>
        <message>
            <source>按键 0x{:X} 不能用作全局热键。请改用字母 A-Z、数字 0-9、空格或 F1-F24</source>
            <translation>Key 0x{:X} can't be used as a global hotkey. Use a letter A-Z, a digit 0-9, Space or F1-F24.</translation>
        </message>
        <message>
            <source>全局热键至少要带一个修饰键：单键 {} 会把其它程序的普通按键吞掉。请加上 Ctrl 或 Alt</source>
            <translation>A global hotkey needs at least one modifier: the single key {} would swallow other programs' ordinary keystrokes. Add Ctrl or Alt.</translation>
        </message>
        <message>
            <source>找遍了设置项 {}、环境变量 TESSDATA_PREFIX 与这些目录都没有语言包：{}。语言包本该随安装包一起装好，请先试重新安装本程序；也可以把 eng.traineddata（要认中文再放 chi_sim.traineddata）放进其中一处，或在设置的 OCR 页把语言包目录指过去</source>
            <translation>None of these has a language pack: the setting {}, the TESSDATA_PREFIX environment variable, the folders {}. One should have come with the installer, so try reinstalling this program first; or put eng.traineddata (add chi_sim.traineddata to read Chinese) into one of them, or point the OCR page of Settings at that folder</translation>
        </message>
        <message>
            <source>语言包已就位于 {}，但本次构建没有编入任何识别后端。见 docs/OcrBackends.md：这是 Debug 档的形状，Release 档带真后端</source>
            <translation>The language pack is in place at {}, but this build compiled in no recognition backend. See docs/OcrBackends.md: that is the Debug shape, the Release build carries the real backend.</translation>
        </message>
        <message>
            <source>这一张图里没认出文字。可以换一种语言再认一次，或重新截一张更清晰的图</source>
            <translation>No text was recognized in this image. Recognize it in another language, or capture a sharper image.</translation>
        </message>
        <message>
            <source>未提供</source>
            <translation>not provided</translation>
        </message>
        <message>
            <source>{} 行 · 平均置信度 {} · 图像 {}×{} · 语言 {}</source>
            <translation>{} lines · average confidence {} · image {}×{} · language {}</translation>
        </message>
        <message>
            <source>识别结果</source>
            <translation>Recognition result</translation>
        </message>
        <message>
            <source>识别文本</source>
            <translation>Recognized text</translation>
        </message>
        <message>
            <source>识别摘要</source>
            <translation>Recognition summary</translation>
        </message>
        <message>
            <source>复制全部</source>
            <translation>Copy all</translation>
        </message>
        <message>
            <source>复制选中</source>
            <translation>Copy selection</translation>
        </message>
        <message>
            <source>导出 TXT</source>
            <translation>Export TXT</translation>
        </message>
        <message>
            <source>导出 JSON</source>
            <translation>Export JSON</translation>
        </message>
        <message>
            <source>关闭</source>
            <translation>Close</translation>
        </message>
        <message>
            <source>这一张图没有认出可复制的文字</source>
            <translation>No copyable text was recognized in this image</translation>
        </message>
        <message>
            <source>已复制全部文字</source>
            <translation>Copied all text</translation>
        </message>
        <message>
            <source>先在右边选中要复制的文字</source>
            <translation>Select the text to copy on the right first</translation>
        </message>
        <message>
            <source>已复制选中文字</source>
            <translation>Copied the selected text</translation>
        </message>
        <message>
            <source>文本文件 (*.txt)</source>
            <translation>Text files (*.txt)</translation>
        </message>
        <message>
            <source>导出 TXT 失败：{}。请换一个目录再试一次</source>
            <translation>Exporting TXT failed: {}. Pick another directory and try again.</translation>
        </message>
        <message>
            <source>已导出 TXT：</source>
            <translation>Exported TXT: </translation>
        </message>
        <message>
            <source>JSON 文件 (*.json)</source>
            <translation>JSON files (*.json)</translation>
        </message>
        <message>
            <source>导出 JSON 失败：{}。请换一个目录再试一次</source>
            <translation>Exporting JSON failed: {}. Pick another directory and try again.</translation>
        </message>
        <message>
            <source>已导出 JSON：</source>
            <translation>Exported JSON: </translation>
        </message>
        <message>
            <source>识别结果写不出去：{}（{}）。请换一个可写的目录，或直接复制到剪贴板</source>
            <translation>The recognition result can't be written: {} ({}). Pick a writable directory, or copy it to the clipboard instead.</translation>
        </message>
        <message>
            <source>识别结果没落盘：{}（{}）。请检查磁盘空间后重试</source>
            <translation>The recognition result wasn't saved to disk: {} ({}). Check the free disk space and retry.</translation>
        </message>
        <message>
            <source>语言包目录 {} 里没有本程序认识的 traineddata。请至少放入 eng.traineddata（要认中文再加 chi_sim.traineddata），或在设置的 OCR 页把目录指过去</source>
            <translation>The language pack folder {} holds no traineddata this program knows. Add at least eng.traineddata (plus chi_sim.traineddata to read Chinese), or point the folder at the OCR page in Settings.</translation>
        </message>
        <message>
            <source>Tesseract 句柄没建出来（内存分配失败）。请重启本程序，若仍然如此请改用系统自带识别或稍后再试</source>
            <translation>The Tesseract handle couldn't be created (memory allocation failed). Restart GyanisScribe; if it still fails, use the recognition that ships with the system or try again later.</translation>
        </message>
        <message>
            <source>语言「{}」需要的模型文件不在：{}。请在设置的 OCR 页改选已有语言，或把该文件放进语言包目录</source>
            <translation>The model file needed by language "{}" is missing: {}. Pick a language you already have on the OCR page in Settings, or put that file into the language pack folder.</translation>
        </message>
        <message>
            <source>Tesseract 初始化语言 {} 失败（返回值 {}）。请检查语言包是否完整或与本程序用的库版本一致</source>
            <translation>Tesseract failed to initialize language {} (returned {}). Check that the language pack is complete and matches the Tesseract library version this program uses.</translation>
        </message>
        <message>
            <source>图片太小或为空（{}×{}），认不出字。请至少框住 {}×{} 的区域</source>
            <translation>The image is too small or empty ({}x{}), so no text can be recognized. Frame an area of at least {}x{}.</translation>
        </message>
        <message>
            <source>Tesseract 识别这一张图失败（语言 {}）。请重试一次，或换一种语言再试</source>
            <translation>Tesseract failed on this image (language {}). Try again, or switch to another language.</translation>
        </message>
        <message>
            <source>无可用识别后端</source>
            <translation>No recognition backend available</translation>
        </message>
        <message>
            <source>本程序没有可用的文字识别后端。请在设置里指定 OCR 语言包目录（tessdata），或安装系统自带的识别组件</source>
            <translation>This program has no text recognition backend available. Set the OCR language pack folder (tessdata) in Settings, or install the recognition component that ships with the system</translation>
        </message>
        <message>
            <source>Windows 系统识别</source>
            <translation>Windows text recognition</translation>
        </message>
        <message>
            <source>这台机器的 Windows 没装本程序认识的识别语言包（系统识别一个可用语言都没有）。请在 设置 → 时间和语言 → 语音 里装上「光学字符识别」，或在设置的 OCR 页改用 Tesseract 并放好语言包</source>
            <translation>This Windows has no text-recognition language pack this app understands (the system engine reports no usable language). Install Optical Character Recognition under Settings -&gt; Time &amp; Language -&gt; Speech, or switch to Tesseract on the OCR settings page and place its language packs</translation>
        </message>
        <message>
            <source>系统里没有「{}」的识别语言包。现在能用的是 {}。请在 设置 → 时间和语言 → 语音 里装上需要的语言包，或在设置的 OCR 页改选语言</source>
            <translation>There is no system language pack for "{}". Usable right now: {}. Install the pack under Settings -&gt; Time &amp; Language -&gt; Speech, or pick another language on the OCR settings page</translation>
        </message>
        <message>
            <source>这一张图没能编成 PNG。请重试一次，或先在标注窗里另存一份 PNG 再识别</source>
            <translation>This image could not be encoded as PNG. Try again, or save a PNG copy from the annotation window and recognise that file</translation>
        </message>
        <message>
            <source>这张图放大到识别尺寸（{}×{}）后超过系统上限（最长边 {}）。请分段截图后再识别，或先在标注窗里裁掉不需要的部分</source>
            <translation>Enlarged for recognition this image measures {}×{}, over the system limit of {} on the longest side. Capture it in parts, or crop what you don't need in the annotation window first</translation>
        </message>
        <message>
            <source>系统说装有「{}」的识别语言，但建不出识别器。请在 设置 → 时间和语言 → 语音 里重装该语言包</source>
            <translation>The system reports a language pack for "{}", but no recognizer could be created. Reinstall that pack under Settings -&gt; Time &amp; Language -&gt; Speech</translation>
        </message>
        <message>
            <source>系统识别这一张图失败（{}，错误码 0x{}）。请重试一次，或改用 Tesseract 后端</source>
            <translation>System recognition failed for this image ({} , error code 0x{}). Try again, or switch to the Tesseract backend</translation>
        </message>
        <message>
            <source>颜色写法 "{}" 不认识。可用取值只有 Hex、Rgb 与 Hsl（区分大小写），请在设置的取色页里重选一次</source>
            <translation>The color format "{}" is unknown. Only Hex, Rgb and Hsl are valid (case-sensitive); pick one again on the color picker page in Settings.</translation>
        </message>
        <message>
            <source>取色器</source>
            <translation>Color picker</translation>
        </message>
        <message>
            <source>取色遮罩</source>
            <translation>Color picker overlay</translation>
        </message>
        <message>
            <source>贴图布局不是一段 JSON 数组：{}。请在设置里清除这一项，让程序重新记一次</source>
            <translation>The pin layout isn't a JSON array: {}. Clear this entry in Settings and let the program record it again.</translation>
        </message>
        <message>
            <source>贴图布局第 {} 项不是对象。请清除这一项后让程序重新记一次</source>
            <translation>Pin layout entry {} is not an object. Clear this entry and let the program record it again.</translation>
        </message>
        <message>
            <source>贴图布局第 {} 项缺少字段 {}。请清除这一项后让程序重新记一次</source>
            <translation>Pin layout entry {} is missing the field {}. Clear this entry and let the program record it again.</translation>
        </message>
        <message>
            <source>贴图布局第 {} 项没有图片位置。请清除这一项后让程序重新记一次</source>
            <translation>Pin layout entry {} has no image location. Clear this entry and let the program record it again.</translation>
        </message>
        <message>
            <source>贴图布局第 {} 项的尺寸不是正数（{}×{}）。请清除这一项后让程序重新记一次</source>
            <translation>Pin layout entry {} has no positive size ({}x{}). Clear this entry and let the program record it again.</translation>
        </message>
        <message>
            <source>贴图布局第 {} 项的不透明度不在 (0,1] 里：{}。请清除这一项后让程序重新记一次</source>
            <translation>Pin layout entry {} has an opacity outside (0,1]: {}. Clear this entry and let the program record it again.</translation>
        </message>
        <message>
            <source>贴图布局第 {} 项的分组号不是正整数：{}。请清除这一项后让程序重新记一次</source>
            <translation>Entry {} of the pin layout has a group number that is not a positive integer: {}. Clear that entry and let the app record it again</translation>
        </message>
        <message>
            <source>贴图布局第 {} 项的置顶字段不是布尔值。请清除这一项后让程序重新记一次</source>
            <translation>Entry {} of the pin layout has a non-boolean always-on-top field. Clear that entry and let the app record it again</translation>
        </message>
        <message>
            <source>贴图布局第 {} 项的穿透字段不是布尔值。请清除这一项后让程序重新记一次</source>
            <translation>Entry {} of the pin layout has a non-boolean mouse-through field. Clear that entry and let the app record it again</translation>
        </message>
        <message>
            <source>贴图默认不透明度 "{}" 不在 1 到 100 之间：请在设置的贴图页重选一个值，或删掉 Pin/DefaultOpacity 这一项回到完全不透明</source>
            <translation>Pin default opacity "{}" is not between 1 and 100: pick a value again on the Pin page of Settings, or remove the Pin/DefaultOpacity entry to go back to fully opaque</translation>
        </message>
        <message>
            <source>现在只有 1 个贴图分组，没有下一组可切。在贴图的右键菜单里选「移动到分组 → 新建分组」才会多出一组</source>
            <translation>There is only one pin group right now, so there is no next group. Choose "Move to group &gt; New group" in a pin's context menu to create one</translation>
        </message>
        <message>
            <source>要移到分组的贴图不在册：索引 {}。请重新在贴图上打开菜单</source>
            <translation>The pin to move is not registered: index {}. Open the menu on that pin again</translation>
        </message>
        <message>
            <source>这台机器的 Qt 写不出「{}」这一档保存格式。请在设置的截图页改选一档</source>
            <translation>This Qt build cannot write the "{}" save format. Pick another one on the Shot page of Settings</translation>
        </message>
        <message>
            <source>智录贴图</source>
            <translation>GyanisScribe pin</translation>
        </message>
        <message>
            <source>贴图窗口</source>
            <translation>Pin window</translation>
        </message>
        <message>
            <source>{}不支持鼠标穿透：这一项在本平台没有实现。请把贴图挪到不挡操作的位置，或降低透明度后从别处点击</source>
            <translation>{} doesn't support click-through: this isn't implemented on this platform. Move the pinned image where it doesn't block your clicks, or lower its opacity and click from somewhere else.</translation>
        </message>
        <message>
            <source>贴图保存失败：{}。请检查目录是否可写，或先把这一张复制进剪贴板</source>
            <translation>Saving the pinned image failed: {}. Check that the directory is writable, or copy this image to the clipboard first.</translation>
        </message>
        <message>
            <source>复制</source>
            <translation>Copy</translation>
        </message>
        <message>
            <source>保存</source>
            <translation>Save</translation>
        </message>
        <message>
            <source>在文件夹中查看</source>
            <translation>Show in folder</translation>
        </message>
        <message>
            <source>取色</source>
            <translation>Pick color</translation>
        </message>
        <message>
            <source>缩到小图</source>
            <translation>Shrink to thumbnail</translation>
        </message>
        <message>
            <source>贴边隐藏</source>
            <translation>Hide at screen edge</translation>
        </message>
        <message>
            <source>移动到分组</source>
            <translation>Move to group</translation>
        </message>
        <message>
            <source>分组 %1</source>
            <translation>Group %1</translation>
        </message>
        <message>
            <source>新建分组</source>
            <translation>New group</translation>
        </message>
        <message>
            <source>保持置顶</source>
            <translation>Keep on top</translation>
        </message>
        <message>
            <source>鼠标穿透</source>
            <translation>Click-through</translation>
        </message>
        <message>
            <source>滚轮缩放，Ctrl+滚轮旋转，Shift+滚轮调透明度，R 转正一挡（Shift+R 反向），0 或双击回到原始比例，Esc 关闭</source>
            <translation>Scroll to scale, Ctrl+scroll to rotate, Shift+scroll for opacity, R to turn a quarter step (Shift+R the other way), 0 or double-click to restore the original size and angle, Esc to close</translation>
        </message>
        <message>
            <source>Linux（降级实现）</source>
            <translation>Linux (degraded implementation)</translation>
        </message>
        <message>
            <source>本平台取不出当前前台窗口（X11 与 Wayland 各有各的会话协议，本仓没接）。请在托盘改用「框选截图」把那个窗口框出来</source>
            <translation>This platform cannot report the foreground window (X11 and Wayland each need their own session protocol, and neither is wired up here). Use region capture in the tray and frame that window instead</translation>
        </message>
        <message>
            <source>本平台没有接开机启动（X11 与 Wayland 的会话各自管启动项，本仓没接）。可以把本程序自己加进桌面环境的「启动应用程序」</source>
            <translation>Autostart is not wired up on this platform (X11 and Wayland sessions each manage startup items, and this repository does not). You can add the program to your desktop environment's start-up applications yourself</translation>
        </message>
        <message>
            <source>本平台列不出屏上的窗口，遮罩上不认「悬停选中整扇窗口」。请直接把要的那一块框出来，或按一次截图键取整屏</source>
            <translation>This platform can't list the windows on screen, so the overlay doesn't recognise "hover to pick a whole window". Frame the part you want directly, or press the capture key once for the full screen</translation>
        </message>
        <message>
            <source>本平台没有全局热键能力：登记不会生效。请在程序窗口内使用普通快捷键，或等平台适配后再开启这一项</source>
            <translation>This platform has no global hotkey support: registering won't take effect. Use ordinary shortcuts inside the program window, or enable this once the platform is adapted.</translation>
        </message>
        <message>
            <source>本平台没有全局热键能力：没有任何登记需要注销</source>
            <translation>This platform has no global hotkey support: nothing is registered to unregister</translation>
        </message>
        <message>
            <source>换算窗口位置要有一个大于 0 的缩放因子，收到的是 {}。请把这块屏自己的读数（QScreen::devicePixelRatio）交进来</source>
            <translation>Converting a window position needs a scaling factor greater than 0; got {}. Pass this screen's own reading (QScreen::devicePixelRatio)</translation>
        </message>
        <message>
            <source>热键编号 {} 在本进程内已经登记过。请换一个编号，或先注销那一条</source>
            <translation>Hotkey id {} is already registered in this process. Use another id, or unregister that entry first.</translation>
        </message>
        <message>
            <source>全局热键登记失败（系统错误码 {}）：这个组合键多半已被其它程序占用。请在设置里换一个组合，或先退出占用它的程序</source>
            <translation>Global hotkey registration failed (system error code {}): another program most likely already holds this combination. Pick another combination in Settings, or close the program that holds it.</translation>
        </message>
        <message>
            <source>热键编号 {} 没有登记过，无需注销</source>
            <translation>Hotkey id {} was never registered, so there is nothing to unregister</translation>
        </message>
        <message>
            <source>热键注销失败（系统错误码 {}）：编号 {}。请重试或重启本程序</source>
            <translation>Unregistering the hotkey failed (system error code {}): id {}. Retry, or restart GyanisScribe.</translation>
        </message>
        <message>
            <source>修饰键无法用于全局热键：掩码 0x{:X}。请只用 Ctrl、Alt、Shift、Win 四种组合</source>
            <translation>Modifiers alone can't be a global hotkey: mask 0x{:X}. Use only combinations of Ctrl, Alt, Shift and Win.</translation>
        </message>
        <message>
            <source>按键 0x{:X} 不能用作全局热键。请改用字母、数字、空格或 F1-F24 之一</source>
            <translation>Key 0x{:X} can't be used as a global hotkey. Use a letter, a digit, Space or one of F1-F24.</translation>
        </message>
        <message>
            <source>现在没有一个在前台的窗口可截（可能锁了屏或焦点在桌面上）。请先点一下要截的那个窗口，再回来用这一项</source>
            <translation>No window is in the foreground right now (the screen may be locked, or focus is on the desktop). Click the window you want first, then use this item again</translation>
        </message>
        <message>
            <source>认不出桌面上有哪些窗口：平台的屏幕清单与界面的屏幕清单配不上对。请先用整屏或框选截图，重开一次智录再试</source>
            <translation>Can't tell which windows are on the desktop: the platform's screen list and the interface's screen list didn't pair up. Use full-screen or region capture for now, then restart GyanisScribe</translation>
        </message>
        <message>
            <source>取不到本程序的完整路径，没法写开机启动项。请从安装目录启动本程序后再试一次</source>
            <translation>The program's full path is unavailable, so the startup item cannot be written. Start the program from its install folder and try again</translation>
        </message>
        <message>
            <source>开机启动项没能{}：写系统设置失败（可能是权限或安全软件拦截）。可以在系统的「启动」设置里把本程序自己加进去</source>
            <translation>Could not {} the startup item: writing the system setting failed (permissions or security software may be blocking it). You can add the program to the system's Startup settings yourself</translation>
        </message>
        <message>
            <source>开启</source>
            <translation>enable</translation>
        </message>
        <message>
            <source>光标高亮的缩放因子必须是正数，收到的是 {}。请传这一块屏幕的 devicePixelRatio</source>
            <translation>The cursor highlight scaling factor must be a positive number; got {}. Pass this screen's devicePixelRatio</translation>
        </message>
        <message>
            <source>光标高亮的目标帧是空的：抓帧没成功时不该来画圈。请先确认抓到的帧，再调用这一步</source>
            <translation>The cursor highlight target frame is empty: do not draw a ring when the grab did not succeed. Check the captured frame before calling this step</translation>
        </message>
        <message>
            <source>列不出音频设备：ffmpeg 的输出里没有认得出的设备行（末段：{}）。请更新 ffmpeg 后重试，录屏仍可静音进行</source>
            <translation>Couldn't list audio devices: ffmpeg's output had no recognizable device line (tail: {}). Update ffmpeg and try again; recording can still run without sound</translation>
        </message>
        <message>
            <source>这个平台不经 dshow 列音频设备（Linux 走 ALSA/Pulse，还没接）。录屏可以照录，只是带不了声音</source>
            <translation>This platform doesn't list audio devices through dshow (Linux uses ALSA/Pulse, not wired up yet). Recording still works, just without sound</translation>
        </message>
        <message>
            <source>列音频设备的那一步起不动 ffmpeg。请确认它还能运行，或重新安装</source>
            <translation>ffmpeg wouldn't start while listing audio devices. Check that it still runs, or reinstall it</translation>
        </message>
        <message>
            <source>要取声音却一路设备都没给。请在设置的录屏页选一路，或把取声关掉</source>
            <translation>Sound was asked for but neither input got a device. Pick one on the Recording page of Settings, or turn audio capture off</translation>
        </message>
        <message>
            <source>没找到 ffmpeg 命令行程序，录屏的编码这一步要用它。它本该随安装包一起装好，请重新安装本程序；也可以自行安装一份并把所在目录加入 PATH，然后重启本程序</source>
            <translation>ffmpeg wasn't found, and the encoding step of the recording needs it. It should have come with the installer, so reinstall this program; or install your own copy, add its folder to PATH, then restart this program</translation>
        </message>
        <message>
            <source>第一帧读不出尺寸（{}）。请确认录屏期间磁盘没满，或先用剪贴板交付这一帧序列</source>
            <translation>The first frame's size can't be read ({}). Check that the disk wasn't full during the recording, or deliver this frame sequence to the clipboard first</translation>
        </message>
        <message>
            <source>编码器探测</source>
            <translation>Encoder probe</translation>
        </message>
        <message>
            <source>MP4 编码</source>
            <translation>MP4 encoding</translation>
        </message>
        <message>
            <source>GIF 调色板</source>
            <translation>GIF palette</translation>
        </message>
        <message>
            <source>GIF 编码</source>
            <translation>GIF encoding</translation>
        </message>
        <message>
            <source>ffmpeg 报告成功，但成品不可用：{}。请检查磁盘空间后重编码，或先用剪贴板交付这一帧序列</source>
            <translation>ffmpeg reported success, but the output is unusable: {}. Check the free disk space and re-encode, or deliver this frame sequence through the clipboard.</translation>
        </message>
        <message>
            <source>{} 起不动 ffmpeg（{}）。请确认该文件还能运行，或重新安装</source>
            <translation>ffmpeg can't be started from {} ({}). Check that the file still runs, or reinstall it.</translation>
        </message>
        <message>
            <source>{} 超时（{} 毫秒未完成）。可以缩短录屏时长，或先把成品交剪贴板</source>
            <translation>{} timed out (still unfinished after {} ms). Shorten the recording, or hand the output to the clipboard first.</translation>
        </message>
        <message>
            <source>{} 失败（退出码 {}）：{}。完整输出在日志里，也可以自己敲一遍同样的命令看细节</source>
            <translation>{} failed (exit code {}): {}. The full output is in the log, and you can also run the same command yourself to see the details.</translation>
        </message>
        <message>
            <source>你这份 ffmpeg 里没有能开起来的视频编码器（试过 {}）。请换装一个带 libx264 的完整 ffmpeg 构建，或者先把这一帧序列自己用 ffmpeg 编一次看报什么</source>
            <translation>None of the video encoders in your ffmpeg build could start (tried {}). Install a full ffmpeg build that carries libx264, or encode this frame sequence by hand with ffmpeg to read what it reports</translation>
        </message>
        <message>
            <source>录屏参数 {}：{}</source>
            <translation>Recording option {}: {}</translation>
        </message>
        <message>
            <source>要写清录哪块屏幕。请在录屏面板重新选一次目标屏幕</source>
            <translation>Say which screen to record. Pick the target screen again in the recording panel.</translation>
        </message>
        <message>
            <source>取值要在 1 到 {} 之间（{} 格式的上限），当前是 {}。帧率越高文件越大、编码越慢</source>
            <translation>The value must be between 1 and {} (the limit of the {} format); it is currently {}. A higher frame rate means a bigger file and slower encoding.</translation>
        </message>
        <message>
            <source>取值要在 0 到 {} 之间（0 表示按质量优先），当前是 {}。码率越高文件越大</source>
            <translation>The value must be between 0 and {} (0 means quality first); it is currently {}. A higher bitrate means a larger file</translation>
        </message>
        <message>
            <source>取值要在 1 到 {} 之间，当前是 {}。留一个上限是防止录屏忘了关，真需要更长就分两段录</source>
            <translation>The value must be between 1 and {}; it is currently {}. The cap keeps a forgotten recording from running on; if you truly need longer, record in two takes.</translation>
        </message>
        <message>
            <source>不能为空。请在设置的录屏页选一个可写目录</source>
            <translation>Cannot be empty. Pick a writable directory on the recording page in Settings.</translation>
        </message>
        <message>
            <source>选区模式需要一个非空矩形，当前是空矩形。请重新框选，或改用整屏模式</source>
            <translation>Region mode needs a non-empty rectangle; the current one is empty. Frame a new selection, or use full-screen mode.</translation>
        </message>
        <message>
            <source>窗口模式需要一个有效句柄，当前是 0。请在目标窗口还在的时候重新选一次</source>
            <translation>Window mode needs a valid handle; the current one is 0. Pick the window again while the target window is still open.</translation>
        </message>
        <message>
            <source>默认帧率 "{}" 不在可用范围（1 到 {}）里：请在设置的录屏页重选一个值，或删掉 Record/DefaultFrameRate 这一项回到默认档</source>
            <translation>Default frame rate "{}" is outside the usable range (1 to {}): pick a value again on the Recording page of Settings, or remove the Record/DefaultFrameRate entry to go back to the default</translation>
        </message>
        <message>
            <source>画面码率 "{}" 不在可用范围（0 到 {} kbps，0 表示按质量优先）里：请在设置的录屏页重选，或删掉 Record/VideoBitrateKbps 这一项回到默认档</source>
            <translation>Video bitrate "{}" is not in the available range (0 to {} kbps, where 0 means quality first): pick a value again on the recording page of Settings, or delete the Record/VideoBitrateKbps entry to fall back to the default</translation>
        </message>
        <message>
            <source>录屏页</source>
            <translation>recording</translation>
        </message>
        <message>
            <source>整屏</source>
            <translation>Full screen</translation>
        </message>
        <message>
            <source>上次选区</source>
            <translation>Last selection</translation>
        </message>
        <message>
            <source>当前窗口</source>
            <translation>Current window</translation>
        </message>
        <message>
            <source>MP4 视频</source>
            <translation>MP4 video</translation>
        </message>
        <message>
            <source>GIF 动图</source>
            <translation>GIF animation</translation>
        </message>
        <message>
            <source>未在录制</source>
            <translation>Not recording</translation>
        </message>
        <message>
            <source>准备开始</source>
            <translation>Getting ready</translation>
        </message>
        <message>
            <source>录制中</source>
            <translation>Recording</translation>
        </message>
        <message>
            <source>已暂停（时长不计）</source>
            <translation>Paused (time not counted)</translation>
        </message>
        <message>
            <source>已录 %1</source>
            <translation>Recorded %1</translation>
        </message>
        <message>
            <source>已中止</source>
            <translation>Aborted</translation>
        </message>
        <message>
            <source>录屏控制条</source>
            <translation>Recording control bar</translation>
        </message>
        <message>
            <source>录制指示</source>
            <translation>Recording indicator</translation>
        </message>
        <message>
            <source>录制状态</source>
            <translation>Recording status</translation>
        </message>
        <message>
            <source>已录时长</source>
            <translation>Time recorded</translation>
        </message>
        <message>
            <source>放弃这一段，不保存成品</source>
            <translation>Discard this take without saving</translation>
        </message>
        <message>
            <source>取消录屏</source>
            <translation>Cancel recording</translation>
        </message>
        <message>
            <source>暂停</source>
            <translation>Pause</translation>
        </message>
        <message>
            <source>停止</source>
            <translation>Stop</translation>
        </message>
        <message>
            <source>停止并保存成品</source>
            <translation>Stop and save the recording</translation>
        </message>
        <message>
            <source>停止并保存</source>
            <translation>Stop and save</translation>
        </message>
        <message>
            <source>最小化到托盘</source>
            <translation>Minimize to tray</translation>
        </message>
        <message>
            <source>倒计时</source>
            <translation>Countdown</translation>
        </message>
        <message>
            <source>继续</source>
            <translation>Resume</translation>
        </message>
        <message>
            <source>上一轮录屏还没收尾。请先停止它，再开始新的一轮</source>
            <translation>The previous recording hasn't finished. Stop it first, then start a new one.</translation>
        </message>
        <message>
            <source>当前没有正在进行的录屏</source>
            <translation>No recording is in progress</translation>
        </message>
        <message>
            <source>这一轮已经停止并在编成品：取消只对在录的那几秒有效，不想要成品就等它编完再删</source>
            <translation>This round already stopped and is encoding now: cancel only applies while you are recording. If you don't want the file, delete it after it finishes.</translation>
        </message>
        <message>
            <source>取声的 ffmpeg 起不来。设备可能正被别的程序占用，关掉占用者再录，或在设置里把取声关掉</source>
            <translation>The audio ffmpeg wouldn't start. The device may be held by another program - close it and try again, or turn audio capture off in Settings</translation>
        </message>
        <message>
            <source>第 {} 帧是空图，写不了。请检查抓帧那一层有没有拿到画面</source>
            <translation>Frame {} is an empty image and can't be written. Check whether the frame-capture layer is getting the screen at all.</translation>
        </message>
        <message>
            <source>录屏帧写入失败：{}。请检查该目录是否可写、磁盘是否已满</source>
            <translation>Writing the recording frame failed: {}. Check that the directory is writable and the disk isn't full.</translation>
        </message>
        <message>
            <source>这一轮一帧都没有存下来，可能是刚开始就按了停止</source>
            <translation>Not one frame was saved for this recording; you probably pressed stop right after the start.</translation>
        </message>
        <message>
            <source>配置项 {} 存不进去：后端返回了错误状态，请检查应用配置目录是否可写</source>
            <translation>Setting {} couldn't be stored: the backend returned an error state. Check that the application settings directory is writable.</translation>
        </message>
        <message>
            <source>写入配置项失败：键名为空。请传「分组/大驼峰」形式的键，例如 Paths/Screenshot</source>
            <translation>Writing the setting failed: the key name is empty. Pass a key as "Group/PascalCase", for example Paths/Screenshot.</translation>
        </message>
        <message>
            <source>配置导出文件打不开：{}（{}）。请换一个可写的目录再试</source>
            <translation>The settings export file can't be opened: {} ({}). Pick a writable directory and try again.</translation>
        </message>
        <message>
            <source>配置导出没落盘：{}（{}）。请检查磁盘空间与目录权限</source>
            <translation>The settings export wasn't saved to disk: {} ({}). Check the free disk space and the directory permissions.</translation>
        </message>
        <message>
            <source>配置导入文件读不到：{}（{}）。请确认路径与文件名</source>
            <translation>The settings import file can't be read: {} ({}). Check the path and the file name.</translation>
        </message>
        <message>
            <source>配置导入文件不是一个 JSON 对象：{}（{}）。请导出本程序生成的那份再导入</source>
            <translation>The settings import file isn't a JSON object: {} ({}). Import the file this program exported instead.</translation>
        </message>
        <message>
            <source>点击后按下快捷键</source>
            <translation>Click, then press the shortcut</translation>
        </message>
        <message>
            <source>%1 的全局热键。点击这一格后按下组合键即可替换，按 Esc 取消</source>
            <translation>Global hotkey for %1. Click this cell and press the combination to replace it; press Esc to cancel.</translation>
        </message>
        <message>
            <source>写入失败</source>
            <translation>Writing failed</translation>
        </message>
        <message>
            <source>请按下快捷键（Esc 取消）</source>
            <translation>Press the shortcut (Esc cancels)</translation>
        </message>
        <message>
            <source>主题取值 "{}" 不在可用档位里：请在设置的主题页从 Light、Dark、System、HighContrast 四项里选一项，手写配置文件时只能是这四个词（区分大小写）</source>
            <translation>Theme value "{}" is not one of the available options: pick Light, Dark, System, or HighContrast on the Theme page in Settings, or write exactly one of those four words (case-sensitive) when editing the config file by hand</translation>
        </message>
        <message>
            <source>提示</source>
            <translation>Notice</translation>
        </message>
        <message>
            <source>提示图标</source>
            <translation>Notice icon</translation>
        </message>
</context>
</TS>
