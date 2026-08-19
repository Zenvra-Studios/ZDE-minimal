#import <Cocoa/Cocoa.h>

#include "Platform/Cocoa/Components/CocoaPromptDialog.h"
#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace Zenvra::Platform::Cocoa::Components
{

static void init_templates(std::vector<TemplateCategory>& categories)
{
    categories.clear();

    // 1. C/C++ Category
    TemplateCategory cpp_cat{
        "cpp", "C/C++", "Assets/icons/material-icon-theme/cpp.svg",
        {
            {"cpp_file", "C++ File (.cpp)", "Source.cpp", ".cpp", "C/C++",
             "Creates a file containing C++ source code.",
             "Assets/icons/material-icon-theme/cpp.svg",
             "#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello from ZDE!\" << std::endl;\n    return 0;\n}\n"},
            {"h_file", "Header File (.h)", "Header.h", ".h", "C/C++",
             "Creates a C/C++ header file with include guards.",
             "Assets/icons/material-icon-theme/h.svg",
             "#pragma once\n\nnamespace Name\n{\n\n}\n"},
            {"cpp_class", "C++ Class", "MyClass.h", ".h", "C/C++",
             "Creates a C++ class declaration with constructor and destructor.",
             "Assets/icons/material-icon-theme/cpp.svg",
             "#pragma once\n\nnamespace Name\n{\n\nclass MyClass\n{\npublic:\n    MyClass() = default;\n    ~MyClass() = default;\n\nprivate:\n};\n\n}\n"},
            {"ixx_file", "C++ Module Interface (.ixx)", "Module.ixx", ".ixx", "C/C++",
             "Creates a modern C++20 module interface unit.",
             "Assets/icons/material-icon-theme/cpp.svg",
             "export module MyModule;\n\nexport namespace MyModule\n{\n    void hello();\n}\n"},
            {"hpp_file", "Header File (.hpp)", "Header.hpp", ".hpp", "C/C++",
             "Creates a C++ template header file.",
             "Assets/icons/material-icon-theme/hpp.svg",
             "#pragma once\n\ntemplate <typename T>\nclass Buffer\n{\npublic:\n    Buffer() = default;\n};\n"}
        }
    };

    // 2. Rust Category
    TemplateCategory rust_cat{
        "rust", "Rust", "Assets/icons/material-icon-theme/rust.svg",
        {
            {"rs_main", "Rust Binary (main.rs)", "main.rs", ".rs", "Rust",
             "Creates a Rust binary application entry point.",
             "Assets/icons/material-icon-theme/rust.svg",
             "fn main() {\n    println!(\"Hello from Rust!\");\n}\n"},
            {"rs_mod", "Rust Module (mod.rs)", "mod.rs", ".rs", "Rust",
             "Creates a Rust module file.",
             "Assets/icons/material-icon-theme/rust.svg",
             "pub fn hello() -> &'static str {\n    \"Hello from module\"\n}\n"},
            {"rs_lib", "Rust Library (lib.rs)", "lib.rs", ".rs", "Rust",
             "Creates a Rust library root with unit tests.",
             "Assets/icons/material-icon-theme/rust.svg",
             "pub fn add(left: usize, right: usize) -> usize {\n    left + right\n}\n"},
            {"cargo_toml", "Cargo Manifest (Cargo.toml)", "Cargo.toml", ".toml", "Rust",
             "Creates a Cargo package configuration manifest.",
             "Assets/icons/material-icon-theme/toml.svg",
             "[package]\nname = \"my_project\"\nversion = \"0.1.0\"\nedition = \"2021\"\n\n[dependencies]\n"}
        }
    };

    // 3. Shaders & Graphics Category
    TemplateCategory shader_cat{
        "shader", "Shaders & Graphics", "Assets/icons/material-icon-theme/shader.svg",
        {
            {"glsl_frag", "GLSL Fragment Shader (.frag)", "shader.frag", ".frag", "Shaders & Graphics",
             "Creates a GLSL fragment shader with standard output.",
             "Assets/icons/material-icon-theme/shader.svg",
             "#version 450 core\n\nin vec2 v_uv;\nout vec4 frag_color;\n\nvoid main()\n{\n    frag_color = vec4(v_uv, 0.5, 1.0);\n}\n"},
            {"glsl_vert", "GLSL Vertex Shader (.vert)", "shader.vert", ".vert", "Shaders & Graphics",
             "Creates a GLSL vertex shader with position input.",
             "Assets/icons/material-icon-theme/shader.svg",
             "#version 450 core\n\nlayout(location = 0) in vec3 a_pos;\nlayout(location = 1) in vec2 a_uv;\n\nout vec2 v_uv;\n\nvoid main()\n{\n    v_uv = a_uv;\n    gl_Position = vec4(a_pos, 1.0);\n}\n"},
            {"glsl_comp", "GLSL Compute Shader (.comp)", "compute.comp", ".comp", "Shaders & Graphics",
             "Creates a GLSL compute shader.",
             "Assets/icons/material-icon-theme/shader.svg",
             "#version 450 core\n\nlayout(local_size_x = 16, local_size_y = 16) in;\n\nvoid main()\n{\n    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);\n}\n"},
            {"hlsl_shader", "HLSL Pixel Shader (.hlsl)", "PixelShader.hlsl", ".hlsl", "Shaders & Graphics",
             "Creates an HLSL pixel shader.",
             "Assets/icons/material-icon-theme/shader.svg",
             "struct PSInput {\n    float4 pos : SV_POSITION;\n    float2 uv  : TEXCOORD0;\n};\n\nfloat4 main(PSInput input) : SV_TARGET\n{\n    return float4(input.uv, 0.0, 1.0);\n}\n"}
        }
    };

    // 4. Build & Config Category
    TemplateCategory build_cat{
        "build", "Build & Config", "Assets/icons/material-icon-theme/cmake.svg",
        {
            {"cmakelists", "CMakeLists (CMakeLists.txt)", "CMakeLists.txt", ".txt", "Build & Config",
             "Creates a CMake project build configuration script.",
             "Assets/icons/material-icon-theme/cmake.svg",
             "cmake_minimum_required(VERSION 3.25)\nproject(MyProject LANGUAGES CXX)\n\nset(CMAKE_CXX_STANDARD 20)\nadd_executable(MyProject Source.cpp)\n"},
            {"json_file", "JSON Configuration (.json)", "config.json", ".json", "Build & Config",
             "Creates a JSON configuration file.",
             "Assets/icons/material-icon-theme/json.svg",
             "{\n    \"name\": \"ZDE-Project\",\n    \"version\": \"1.0.0\"\n}\n"},
            {"toml_file", "TOML Document (.toml)", "settings.toml", ".toml", "Build & Config",
             "Creates a TOML document.",
             "Assets/icons/material-icon-theme/toml.svg",
             "[settings]\ntheme = \"zenvra_dark\"\n"}
        }
    };

    // 5. General Category
    TemplateCategory gen_cat{
        "general", "General", "Assets/icons/material-icon-theme/document.svg",
        {
            {"txt_file", "Text Document (.txt)", "Document.txt", ".txt", "General",
             "Creates an empty plain text document.",
             "Assets/icons/material-icon-theme/document.svg", ""},
            {"md_file", "Markdown Document (.md)", "README.md", ".md", "General",
             "Creates a Markdown documentation file.",
             "Assets/icons/material-icon-theme/markdown.svg",
             "# Project Documentation\n"},
            {"gitignore", "Git Ignore (.gitignore)", ".gitignore", "", "General",
             "Creates standard gitignore rules.",
             "Assets/icons/material-icon-theme/git.svg",
             "build/\n.cache/\n*.o\n*.dylib\n"}
        }
    };

    categories.push_back(std::move(cpp_cat));
    categories.push_back(std::move(rust_cat));
    categories.push_back(std::move(shader_cat));
    categories.push_back(std::move(build_cat));
    categories.push_back(std::move(gen_cat));
}

} // namespace Zenvra::Platform::Cocoa::Components

@interface ZDEPromptWindow : NSWindow
@end

@implementation ZDEPromptWindow
- (BOOL)canBecomeKeyWindow {
    return YES;
}
- (BOOL)canBecomeMainWindow {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
@end

@interface ZDEPromptWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) Zenvra::Platform::Cocoa::Components::CocoaPromptDialog* owner;
@end

@implementation ZDEPromptWindowDelegate
- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    if (self.owner) {
        self.owner->notify_window_closed();
    }
}
- (void)windowDidResignKey:(NSNotification *)notification {
    (void)notification;
    if (self.owner) {
        self.owner->close();
    }
}
@end

@interface ZDEPromptWindowContentView : NSView <NSTextInputClient>
{
@public
    Zenvra::Platform::Cocoa::Components::CocoaPromptDialog* owner;
    Zenvra::Platform::Cocoa::Components::PromptDialogMode mode;
    std::string title;
    std::string subtitle;
    std::string placeholder;
    std::string confirmLabel;
    std::string textValue;
    std::size_t caretPos;
    std::filesystem::path targetFolder;

    std::vector<Zenvra::Platform::Cocoa::Components::TemplateCategory> categories;
    std::size_t selectedCategory;
    std::size_t selectedTemplate;

    std::optional<std::size_t> hoveredCategory;
    std::optional<std::size_t> hoveredTemplate;
    bool okHovered;
    bool cancelHovered;

    bool showCaret;
    NSTimer* blinkTimer;
    id localClickMonitor;
    id globalClickMonitor;

    std::function<void(const std::string&, const std::string&)> onConfirmNewItem;
    std::function<void(const std::string&)> onConfirmString;
    std::function<void()> onConfirmAction;

    std::unordered_map<std::string, CGImageRef> svgImageCache;
}
- (void)submit;
- (void)closeWindow;
- (void)startBlinkTimer;
- (void)stopBlinkTimer;
- (void)cleanupMonitors;
- (void)blinkCaret:(NSTimer*)timer;
- (void)drawSvgIcon:(CGContextRef)context path:(const std::string&)path cx:(int)cx cy:(int)cy size:(int)size;
@end

@implementation ZDEPromptWindowContentView

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    [self startBlinkTimer];
    return YES;
}

- (BOOL)resignFirstResponder {
    [self stopBlinkTimer];
    return YES;
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if ([self window]) {
        [self startBlinkTimer];
        if (!localClickMonitor) {
            __block ZDEPromptWindowContentView* selfPtr = self;
            localClickMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown) handler:^NSEvent *(NSEvent *event) {
                if (selfPtr && [selfPtr window]) {
                    if ([event window] != [selfPtr window]) {
                        [selfPtr closeWindow];
                    }
                }
                return event;
            }];
        }
        if (!globalClickMonitor) {
            __block ZDEPromptWindowContentView* selfPtr = self;
            globalClickMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown) handler:^(NSEvent *event) {
                (void)event;
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (selfPtr && [selfPtr window]) {
                        [selfPtr closeWindow];
                    }
                });
            }];
        }
    } else {
        [self stopBlinkTimer];
        [self cleanupMonitors];
    }
}

- (void)cleanupMonitors {
    if (localClickMonitor) {
        [NSEvent removeMonitor:localClickMonitor];
        localClickMonitor = nil;
    }
    if (globalClickMonitor) {
        [NSEvent removeMonitor:globalClickMonitor];
        globalClickMonitor = nil;
    }
}

- (void)startBlinkTimer {
    [self stopBlinkTimer];
    showCaret = true;
    blinkTimer = [NSTimer scheduledTimerWithTimeInterval:0.5 target:self selector:@selector(blinkCaret:) userInfo:nil repeats:YES];
    [self setNeedsDisplay:YES];
}

- (void)stopBlinkTimer {
    if (blinkTimer) {
        [blinkTimer invalidate];
        blinkTimer = nil;
    }
    showCaret = false;
}

- (void)blinkCaret:(NSTimer*)timer {
    (void)timer;
    showCaret = !showCaret;
    [self setNeedsDisplay:YES];
}

- (void)dealloc {
    [self cleanupMonitors];
    [self stopBlinkTimer];
    for (auto& [path, img] : svgImageCache) {
        if (img) CGImageRelease(img);
    }
    svgImageCache.clear();
    [super dealloc];
}

- (void)closeWindow {
    [self cleanupMonitors];
    [self stopBlinkTimer];
    if (owner) {
        owner->close();
    } else {
        NSWindow* win = [self window];
        if (win) {
            [win orderOut:nil];
            [win close];
        }
    }
}

- (void)submit {
    if (mode == Zenvra::Platform::Cocoa::Components::PromptDialogMode::AddNewItem) {
        if (textValue.empty()) return;
        std::string filename = textValue;
        std::string content;
        if (selectedCategory < categories.size()) {
            const auto& tpls = categories[selectedCategory].templates;
            if (selectedTemplate < tpls.size()) {
                content = tpls[selectedTemplate].default_content;
            }
        }
        auto cb = std::move(onConfirmNewItem);
        [self closeWindow];
        if (cb) cb(filename, content);
    } else if (mode == Zenvra::Platform::Cocoa::Components::PromptDialogMode::ConfirmDelete) {
        auto cb = std::move(onConfirmAction);
        [self closeWindow];
        if (cb) cb();
    } else {
        if (textValue.empty()) return;
        std::string res = textValue;
        auto cb = std::move(onConfirmString);
        [self closeWindow];
        if (cb) cb(res);
    }
}

- (void)drawSvgIcon:(CGContextRef)context path:(const std::string&)relPath cx:(int)cx cy:(int)cy size:(int)size {
    if (relPath.empty()) return;

    CGImageRef cached = svgImageCache[relPath];
    if (!cached) {
        // Resolve path in Resources
        NSString* resPath = [[NSBundle mainBundle] resourcePath];
        std::filesystem::path fullPath = std::filesystem::path([resPath UTF8String]) / relPath;
        if (!std::filesystem::exists(fullPath)) {
            fullPath = std::filesystem::path([resPath UTF8String]) / "Assets" / relPath;
        }
        if (!std::filesystem::exists(fullPath)) {
            fullPath = std::filesystem::current_path() / relPath;
        }

        if (std::filesystem::exists(fullPath)) {
            auto document = lunasvg::Document::loadFromFile(fullPath.string());
            if (document) {
                auto bitmap = document->renderToBitmap(
                    static_cast<std::uint32_t>(size * 2), static_cast<std::uint32_t>(size * 2));
                if (!bitmap.isNull()) {
                    const std::uint32_t width = bitmap.width();
                    const std::uint32_t height = bitmap.height();
                    const auto* src32 = reinterpret_cast<const std::uint32_t*>(bitmap.data());

                    std::vector<std::uint8_t> converted_data(static_cast<std::size_t>(width * height * 4));
                    for (std::uint32_t i = 0; i < width * height; ++i) {
                        const std::uint32_t pixel = src32[i];
                        const std::uint8_t alpha = static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU);
                        const std::uint8_t src_red = static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
                        const std::uint8_t src_green = static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
                        const std::uint8_t src_blue = static_cast<std::uint8_t>(pixel & 0xFFU);
                        converted_data[i * 4 + 0] = src_red;
                        converted_data[i * 4 + 1] = src_green;
                        converted_data[i * 4 + 2] = src_blue;
                        converted_data[i * 4 + 3] = alpha;
                    }

                    CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
                    CGContextRef bitmap_context = CGBitmapContextCreate(
                        converted_data.data(), width, height, 8, width * 4, color_space,
                        static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
                    if (bitmap_context) {
                        cached = CGBitmapContextCreateImage(bitmap_context);
                        CGContextRelease(bitmap_context);
                        svgImageCache[relPath] = cached;
                    }
                    CGColorSpaceRelease(color_space);
                }
            }
        }
    }

    if (cached) {
        CGRect r = CGRectMake(cx - size * 0.5f, cy - size * 0.5f, size, size);
        CGContextSaveGState(context);
        CGContextTranslateCTM(context, 0, r.origin.y + r.size.height + r.origin.y);
        CGContextScaleCTM(context, 1.0, -1.0);
        CGContextDrawImage(context, r, cached);
        CGContextRestoreGState(context);
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    CGContextRef context = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    if (!context) return;

    const NSRect b = [self bounds];
    const float w = static_cast<float>(b.size.width);
    const float h = static_cast<float>(b.size.height);

    CGContextSaveGState(context);

    // 1. Sleek Floating Solid Dark Card Background (Standard macOS 10.0 Radius)
    NSBezierPath* bgPath = [NSBezierPath bezierPathWithRoundedRect:b xRadius:10.0 yRadius:10.0];
    [bgPath addClip];

    CGContextSetRGBFillColor(context, 30.0/255.0, 31.0/255.0, 34.0/255.0, 1.0);
    CGContextFillRect(context, CGRectMake(0, 0, w, h));

    if (mode == Zenvra::Platform::Cocoa::Components::PromptDialogMode::AddNewItem) {
        const float titlebarH = 36.0f;
        const float footerH = 74.0f;
        const float paneH = h - titlebarH - footerH;
        const float catW = 160.0f;
        const float tplW = 340.0f;
        const float detailsX = catW + tplW;
        const float detailsW = w - detailsX;

        // Titlebar Background
        CGContextSetRGBFillColor(context, 26.0/255.0, 27.0/255.0, 30.0/255.0, 1.0);
        CGContextFillRect(context, CGRectMake(0, 0, w, titlebarH));

        CGContextSetRGBStrokeColor(context, 48.0/255.0, 50.0/255.0, 55.0/255.0, 1.0);
        CGContextSetLineWidth(context, 1.0);
        CGContextBeginPath(context);
        CGContextMoveToPoint(context, 0, titlebarH + 0.5);
        CGContextAddLineToPoint(context, w, titlebarH + 0.5);
        CGContextStrokePath(context);

        // Centered Title
        NSString* titleStr = [NSString stringWithUTF8String:title.c_str()];
        NSDictionary* titleAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:13.0 weight:NSFontWeightBold],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:235.0/255.0 green:238.0/255.0 blue:242.0/255.0 alpha:1.0]
        };
        NSSize titleSz = [titleStr sizeWithAttributes:titleAttrs];
        [titleStr drawAtPoint:NSMakePoint((w - titleSz.width) * 0.5f, (titlebarH - titleSz.height) * 0.5f) withAttributes:titleAttrs];

        // Left Category Pane Background
        CGContextSetRGBFillColor(context, 24.0/255.0, 25.0/255.0, 28.0/255.0, 1.0);
        CGContextFillRect(context, CGRectMake(0, titlebarH, catW, paneH));

        // Separators
        CGContextSetRGBStrokeColor(context, 48.0/255.0, 50.0/255.0, 55.0/255.0, 1.0);
        CGContextBeginPath(context);
        CGContextMoveToPoint(context, catW + 0.5, titlebarH);
        CGContextAddLineToPoint(context, catW + 0.5, titlebarH + paneH);
        CGContextMoveToPoint(context, detailsX + 0.5, titlebarH);
        CGContextAddLineToPoint(context, detailsX + 0.5, titlebarH + paneH);
        CGContextStrokePath(context);

        // Categories
        const float itemH = 28.0f;
        for (std::size_t i = 0; i < categories.size(); ++i) {
            const auto& cat = categories[i];
            const NSRect itemRect = NSMakeRect(0, titlebarH + i * itemH, catW, itemH);
            const bool isSel = (i == selectedCategory);
            const bool isHov = (hoveredCategory && *hoveredCategory == i);

            if (isSel) {
                CGContextSetRGBFillColor(context, 53.0/255.0, 132.0/255.0, 228.0/255.0, 1.0);
                CGContextFillRect(context, NSRectToCGRect(itemRect));
            } else if (isHov) {
                CGContextSetRGBFillColor(context, 45.0/255.0, 47.0/255.0, 52.0/255.0, 1.0);
                CGContextFillRect(context, NSRectToCGRect(itemRect));
            }

            [self drawSvgIcon:context path:cat.icon_path cx:itemRect.origin.x + 18 cy:itemRect.origin.y + itemH * 0.5 size:16];

            NSString* catStr = [NSString stringWithUTF8String:cat.name.c_str()];
            NSDictionary* catAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:isSel ? NSFontWeightSemibold : NSFontWeightRegular],
                NSForegroundColorAttributeName: isSel ? [NSColor whiteColor] : [NSColor colorWithSRGBRed:205.0/255.0 green:205.0/255.0 blue:205.0/255.0 alpha:1.0]
            };
            [catStr drawAtPoint:NSMakePoint(itemRect.origin.x + 34, itemRect.origin.y + 6) withAttributes:catAttrs];
        }

        // Templates
        if (selectedCategory < categories.size()) {
            const auto& tpls = categories[selectedCategory].templates;
            for (std::size_t i = 0; i < tpls.size(); ++i) {
                const auto& tpl = tpls[i];
                const NSRect itemRect = NSMakeRect(catW, titlebarH + i * itemH, tplW, itemH);
                const bool isSel = (i == selectedTemplate);
                const bool isHov = (hoveredTemplate && *hoveredTemplate == i);

                if (isSel) {
                    CGContextSetRGBFillColor(context, 53.0/255.0, 132.0/255.0, 228.0/255.0, 1.0);
                    CGContextFillRect(context, NSRectToCGRect(itemRect));
                } else if (isHov) {
                    CGContextSetRGBFillColor(context, 45.0/255.0, 47.0/255.0, 52.0/255.0, 1.0);
                    CGContextFillRect(context, NSRectToCGRect(itemRect));
                }

                [self drawSvgIcon:context path:tpl.icon_path cx:itemRect.origin.x + 18 cy:itemRect.origin.y + itemH * 0.5 size:16];

                NSString* tplStr = [NSString stringWithUTF8String:tpl.name.c_str()];
                NSDictionary* tplAttrs = @{
                    NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:isSel ? NSFontWeightSemibold : NSFontWeightRegular],
                    NSForegroundColorAttributeName: isSel ? [NSColor whiteColor] : [NSColor colorWithSRGBRed:220.0/255.0 green:220.0/255.0 blue:220.0/255.0 alpha:1.0]
                };
                [tplStr drawAtPoint:NSMakePoint(itemRect.origin.x + 34, itemRect.origin.y + 6) withAttributes:tplAttrs];

                NSString* tagStr = [NSString stringWithUTF8String:tpl.category.c_str()];
                NSDictionary* tagAttrs = @{
                    NSFontAttributeName: [NSFont systemFontOfSize:10.5 weight:NSFontWeightRegular],
                    NSForegroundColorAttributeName: isSel ? [NSColor colorWithSRGBRed:208.0/255.0 green:230.0/255.0 blue:255.0/255.0 alpha:1.0] : [NSColor colorWithSRGBRed:120.0/255.0 green:120.0/255.0 blue:132.0/255.0 alpha:1.0]
                };
                NSSize tagSz = [tagStr sizeWithAttributes:tagAttrs];
                [tagStr drawAtPoint:NSMakePoint(itemRect.origin.x + tplW - tagSz.width - 12, itemRect.origin.y + 7) withAttributes:tagAttrs];
            }
        }

        // Details
        if (selectedCategory < categories.size()) {
            const auto& tpls = categories[selectedCategory].templates;
            if (selectedTemplate < tpls.size()) {
                const auto& tpl = tpls[selectedTemplate];

                [self drawSvgIcon:context path:tpl.icon_path cx:detailsX + 24 cy:titlebarH + 28 size:24];

                NSString* typeStr = [NSString stringWithFormat:@"Type: %s", tpl.category.c_str()];
                NSDictionary* typeAttrs = @{
                    NSFontAttributeName: [NSFont systemFontOfSize:12.0 weight:NSFontWeightBold],
                    NSForegroundColorAttributeName: [NSColor whiteColor]
                };
                [typeStr drawAtPoint:NSMakePoint(detailsX + 44, titlebarH + 18) withAttributes:typeAttrs];

                NSString* descStr = [NSString stringWithUTF8String:tpl.description.c_str()];
                NSDictionary* descAttrs = @{
                    NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular],
                    NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:160.0/255.0 green:160.0/255.0 blue:170.0/255.0 alpha:1.0]
                };
                NSRect descRect = NSMakeRect(detailsX + 16, titlebarH + 50, detailsW - 32, paneH - 60);
                [descStr drawWithRect:descRect options:NSStringDrawingUsesLineFragmentOrigin attributes:descAttrs];
            }
        }

        // Footer
        const float footerY = h - footerH;
        CGContextSetRGBStrokeColor(context, 48.0/255.0, 50.0/255.0, 55.0/255.0, 1.0);
        CGContextBeginPath(context);
        CGContextMoveToPoint(context, 0, footerY + 0.5);
        CGContextAddLineToPoint(context, w, footerY + 0.5);
        CGContextStrokePath(context);

        NSString* nameLbl = @"Name:";
        NSDictionary* lblAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:180.0/255.0 green:180.0/255.0 blue:180.0/255.0 alpha:1.0]
        };
        [nameLbl drawAtPoint:NSMakePoint(16, footerY + 18) withAttributes:lblAttrs];

        // Input Box
        const NSRect inputRect = NSMakeRect(64, footerY + 12, w - 248, 28);
        NSBezierPath* inputPath = [NSBezierPath bezierPathWithRoundedRect:inputRect xRadius:6 yRadius:6];
        [[NSColor colorWithSRGBRed:20.0/255.0 green:22.0/255.0 blue:25.0/255.0 alpha:0.5] setFill];
        [inputPath fill];
        [[NSColor colorWithSRGBRed:53.0/255.0 green:132.0/255.0 blue:228.0/255.0 alpha:0.9] setStroke];
        [inputPath stroke];

        NSString* valStr = [NSString stringWithUTF8String:textValue.c_str()];
        NSDictionary* valAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName: [NSColor whiteColor]
        };
        [valStr drawAtPoint:NSMakePoint(inputRect.origin.x + 8, inputRect.origin.y + 6) withAttributes:valAttrs];

        std::size_t cp = std::min(caretPos, textValue.size());
        NSString* prefStr = [NSString stringWithUTF8String:textValue.substr(0, cp).c_str()];
        NSSize prefSz = [prefStr sizeWithAttributes:valAttrs];
        const float caretX = inputRect.origin.x + 8 + prefSz.width;
        if (showCaret) {
            CGContextSetRGBStrokeColor(context, 1.0, 1.0, 1.0, 1.0);
            CGContextSetLineWidth(context, 1.2);
            CGContextBeginPath(context);
            CGContextMoveToPoint(context, caretX + 0.5, inputRect.origin.y + 5);
            CGContextAddLineToPoint(context, caretX + 0.5, inputRect.origin.y + 23);
            CGContextStrokePath(context);
        }

        const NSRect okRect = NSMakeRect(w - 170, footerY + 12, 75, 28);
        NSBezierPath* okPath = [NSBezierPath bezierPathWithRoundedRect:okRect xRadius:6 yRadius:6];
        if (okHovered) {
            [[NSColor colorWithSRGBRed:66.0/255.0 green:148.0/255.0 blue:246.0/255.0 alpha:1.0] setFill];
        } else {
            [[NSColor colorWithSRGBRed:53.0/255.0 green:132.0/255.0 blue:228.0/255.0 alpha:1.0] setFill];
        }
        [okPath fill];
        NSString* okStr = [NSString stringWithUTF8String:confirmLabel.c_str()];
        NSDictionary* okAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: [NSColor whiteColor]
        };
        NSSize okSz = [okStr sizeWithAttributes:okAttrs];
        [okStr drawAtPoint:NSMakePoint(okRect.origin.x + (okRect.size.width - okSz.width) * 0.5, okRect.origin.y + 6) withAttributes:okAttrs];

        const NSRect cancelRect = NSMakeRect(w - 88, footerY + 12, 75, 28);
        NSBezierPath* cancelPath = [NSBezierPath bezierPathWithRoundedRect:cancelRect xRadius:6 yRadius:6];
        if (cancelHovered) {
            [[NSColor colorWithSRGBRed:58.0/255.0 green:61.0/255.0 blue:68.0/255.0 alpha:1.0] setFill];
        } else {
            [[NSColor colorWithSRGBRed:45.0/255.0 green:47.0/255.0 blue:52.0/255.0 alpha:1.0] setFill];
        }
        [cancelPath fill];
        NSString* cnStr = @"Cancel";
        NSDictionary* cnAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:204.0/255.0 green:204.0/255.0 blue:204.0/255.0 alpha:1.0]
        };
        NSSize cnSz = [cnStr sizeWithAttributes:cnAttrs];
        [cnStr drawAtPoint:NSMakePoint(cancelRect.origin.x + (cancelRect.size.width - cnSz.width) * 0.5, cancelRect.origin.y + 6) withAttributes:cnAttrs];

        NSString* locLbl = @"Location:";
        NSDictionary* locLblAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.5 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:136.0/255.0 green:136.0/255.0 blue:144.0/255.0 alpha:1.0]
        };
        [locLbl drawAtPoint:NSMakePoint(16, footerY + 47) withAttributes:locLblAttrs];
        std::string locStr = targetFolder.empty() ? "Project Root" : targetFolder.string();
        NSString* locValStr = [NSString stringWithUTF8String:locStr.c_str()];
        NSDictionary* locValAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.5 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:160.0/255.0 green:160.0/255.0 blue:170.0/255.0 alpha:1.0]
        };
        [locValStr drawAtPoint:NSMakePoint(76, footerY + 47) withAttributes:locValAttrs];

    } else {
        // --- Sleek Minimalist Quick Modal Card (New Folder, Rename, Delete) ---
        // 2. Centered Bold Title (matching screenshot)
        NSString* titleStr = [NSString stringWithUTF8String:title.c_str()];
        NSDictionary* titleAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:13.5 weight:NSFontWeightBold],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:235.0/255.0 green:238.0/255.0 blue:242.0/255.0 alpha:1.0]
        };
        NSSize titleSz = [titleStr sizeWithAttributes:titleAttrs];
        const float titleX = (w - titleSz.width) * 0.5f;
        const float titleY = 16.0f;
        [titleStr drawAtPoint:NSMakePoint(titleX, titleY) withAttributes:titleAttrs];

        if (mode == Zenvra::Platform::Cocoa::Components::PromptDialogMode::ConfirmDelete) {
            NSString* subStr = [NSString stringWithUTF8String:subtitle.c_str()];
            NSDictionary* subAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular],
                NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:160.0/255.0 green:160.0/255.0 blue:170.0/255.0 alpha:1.0]
            };
            NSSize subSz = [subStr sizeWithAttributes:subAttrs];
            [subStr drawAtPoint:NSMakePoint((w - subSz.width) * 0.5f, 42.0f) withAttributes:subAttrs];

            const NSRect cancelRect = NSMakeRect(w * 0.5f - 85, h - 36, 75, 26);
            const NSRect okRect = NSMakeRect(w * 0.5f + 10, h - 36, 75, 26);

            NSBezierPath* cancelPath = [NSBezierPath bezierPathWithRoundedRect:cancelRect xRadius:6 yRadius:6];
            [[NSColor colorWithSRGBRed:cancelHovered ? 58.0/255.0 : 45.0/255.0 green:cancelHovered ? 61.0/255.0 : 47.0/255.0 blue:cancelHovered ? 68.0/255.0 : 52.0/255.0 alpha:1.0] setFill];
            [cancelPath fill];
            [@"Cancel" drawAtPoint:NSMakePoint(cancelRect.origin.x + 18, cancelRect.origin.y + 5) withAttributes:@{
                NSFontAttributeName: [NSFont systemFontOfSize:11.5],
                NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:204.0/255.0 green:204.0/255.0 blue:204.0/255.0 alpha:1.0]
            }];

            NSBezierPath* okPath = [NSBezierPath bezierPathWithRoundedRect:okRect xRadius:6 yRadius:6];
            [[NSColor colorWithSRGBRed:okHovered ? 235.0/255.0 : 218.0/255.0 green:okHovered ? 65.0/255.0 : 45.0/255.0 blue:okHovered ? 70.0/255.0 : 50.0/255.0 alpha:1.0] setFill];
            [okPath fill];
            [@"Delete" drawAtPoint:NSMakePoint(okRect.origin.x + 18, okRect.origin.y + 5) withAttributes:@{
                NSFontAttributeName: [NSFont systemFontOfSize:11.5 weight:NSFontWeightMedium],
                NSForegroundColorAttributeName: [NSColor whiteColor]
            }];
        } else {
            // 3. Pure Transparent Borderless Input Field (matching screenshot)
            const NSRect inputRect = NSMakeRect(22, 46, w - 44, 32);

            NSString* valStr = textValue.empty() ? [NSString stringWithUTF8String:placeholder.c_str()] : [NSString stringWithUTF8String:textValue.c_str()];
            NSDictionary* valAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:13.0 weight:NSFontWeightRegular],
                NSForegroundColorAttributeName: textValue.empty() 
                    ? [NSColor colorWithSRGBRed:110.0/255.0 green:115.0/255.0 blue:130.0/255.0 alpha:1.0] 
                    : [NSColor colorWithSRGBRed:240.0/255.0 green:242.0/255.0 blue:245.0/255.0 alpha:1.0]
            };
            [valStr drawAtPoint:NSMakePoint(inputRect.origin.x + 4, inputRect.origin.y + 7) withAttributes:valAttrs];

            // Animated Caret
            std::size_t cp = std::min(caretPos, textValue.size());
            NSString* prefStr = textValue.empty() ? @"" : [NSString stringWithUTF8String:textValue.substr(0, cp).c_str()];
            NSDictionary* activeAttrs = @{
                NSFontAttributeName: [NSFont systemFontOfSize:13.0 weight:NSFontWeightRegular],
                NSForegroundColorAttributeName: [NSColor whiteColor]
            };
            NSSize prefSz = [prefStr sizeWithAttributes:activeAttrs];
            const float caretX = inputRect.origin.x + 4 + prefSz.width;
            if (showCaret) {
                CGContextSetRGBStrokeColor(context, 1.0, 1.0, 1.0, 0.9);
                CGContextSetLineWidth(context, 1.4);
                CGContextBeginPath(context);
                CGContextMoveToPoint(context, caretX + 0.5, inputRect.origin.y + 6);
                CGContextAddLineToPoint(context, caretX + 0.5, inputRect.origin.y + 26);
                CGContextStrokePath(context);
            }
        }
    }

    CGContextRestoreGState(context);

    // Subtle 1px Outer Border on Card (Standard macOS 10.0 Radius, Solid)
    NSBezierPath* strokePath = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(b, 0.5, 0.5) xRadius:10.0 yRadius:10.0];
    [[NSColor colorWithSRGBRed:58.0/255.0 green:60.0/255.0 blue:68.0/255.0 alpha:1.0] setStroke];
    [strokePath setLineWidth:1.0];
    [strokePath stroke];
}

- (void)mouseDown:(NSEvent *)event {
    [[self window] makeFirstResponder:self];
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect b = [self bounds];
    const float w = static_cast<float>(b.size.width);
    const float h = static_cast<float>(b.size.height);

    if (mode == Zenvra::Platform::Cocoa::Components::PromptDialogMode::AddNewItem) {
        const float titlebarH = 36.0f;
        const float footerH = 74.0f;
        const float paneH = h - titlebarH - footerH;
        const float catW = 160.0f;
        const float tplW = 340.0f;

        // Categories click
        if (loc.x >= 0 && loc.x < catW && loc.y >= titlebarH && loc.y < titlebarH + paneH) {
            std::size_t idx = static_cast<std::size_t>((loc.y - titlebarH) / 28.0f);
            if (idx < categories.size()) {
                selectedCategory = idx;
                selectedTemplate = 0;
                if (!categories[idx].templates.empty()) {
                    textValue = categories[idx].templates[0].default_filename;
                }
                caretPos = textValue.size();
                [self setNeedsDisplay:YES];
                return;
            }
        }

        // Templates click
        if (loc.x >= catW && loc.x < catW + tplW && loc.y >= titlebarH && loc.y < titlebarH + paneH) {
            std::size_t idx = static_cast<std::size_t>((loc.y - titlebarH) / 28.0f);
            if (selectedCategory < categories.size()) {
                const auto& tpls = categories[selectedCategory].templates;
                if (idx < tpls.size()) {
                    selectedTemplate = idx;
                    textValue = tpls[idx].default_filename;
                    caretPos = textValue.size();
                    [self setNeedsDisplay:YES];
                    return;
                }
            }
        }

        const float footerY = h - footerH;
        const NSRect inputRect = NSMakeRect(64, footerY + 12, w - 248, 28);
        if (NSPointInRect(loc, inputRect)) {
            float clickOffset = loc.x - (inputRect.origin.x + 8);
            if (clickOffset <= 0 || textValue.empty()) {
                caretPos = 0;
            } else {
                NSDictionary* valAttrs = @{
                    NSFontAttributeName: [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular]
                };
                caretPos = textValue.size();
                for (std::size_t i = 1; i <= textValue.size(); ++i) {
                    NSString* sub = [NSString stringWithUTF8String:textValue.substr(0, i).c_str()];
                    NSSize sz = [sub sizeWithAttributes:valAttrs];
                    if (sz.width >= clickOffset) {
                        caretPos = i - 1;
                        break;
                    }
                }
            }
            showCaret = true;
            [self startBlinkTimer];
            [self setNeedsDisplay:YES];
            return;
        }

        const NSRect okRect = NSMakeRect(w - 170, footerY + 12, 75, 28);
        const NSRect cancelRect = NSMakeRect(w - 88, footerY + 12, 75, 28);

        if (NSPointInRect(loc, okRect)) {
            [self submit];
            return;
        }
        if (NSPointInRect(loc, cancelRect)) {
            [self closeWindow];
            return;
        }
    } else {
        const NSRect inputRect = NSMakeRect(22, 46, w - 44, 32);
        if (mode != Zenvra::Platform::Cocoa::Components::PromptDialogMode::ConfirmDelete && NSPointInRect(loc, inputRect)) {
            float clickOffset = loc.x - (inputRect.origin.x + 4);
            if (clickOffset <= 0 || textValue.empty()) {
                caretPos = 0;
            } else {
                NSDictionary* valAttrs = @{
                    NSFontAttributeName: [NSFont systemFontOfSize:13.0 weight:NSFontWeightRegular]
                };
                caretPos = textValue.size();
                for (std::size_t i = 1; i <= textValue.size(); ++i) {
                    NSString* sub = [NSString stringWithUTF8String:textValue.substr(0, i).c_str()];
                    NSSize sz = [sub sizeWithAttributes:valAttrs];
                    if (sz.width >= clickOffset) {
                        caretPos = i - 1;
                        break;
                    }
                }
            }
            showCaret = true;
            [self startBlinkTimer];
            [self setNeedsDisplay:YES];
            return;
        }

        if (mode == Zenvra::Platform::Cocoa::Components::PromptDialogMode::ConfirmDelete) {
            const NSRect cancelRect = NSMakeRect(w * 0.5f - 85, h - 36, 75, 26);
            const NSRect okRect = NSMakeRect(w * 0.5f + 10, h - 36, 75, 26);

            if (NSPointInRect(loc, okRect)) {
                [self submit];
                return;
            }
            if (NSPointInRect(loc, cancelRect)) {
                [self closeWindow];
                return;
            }
        }
    }
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect b = [self bounds];
    const float w = static_cast<float>(b.size.width);
    const float h = static_cast<float>(b.size.height);

    bool prevOk = okHovered;
    bool prevCancel = cancelHovered;

    if (mode == Zenvra::Platform::Cocoa::Components::PromptDialogMode::AddNewItem) {
        const float titlebarH = 36.0f;
        const float footerH = 74.0f;
        const float paneH = h - titlebarH - footerH;
        const float catW = 160.0f;
        const float tplW = 340.0f;

        if (loc.x >= 0 && loc.x < catW && loc.y >= titlebarH && loc.y < titlebarH + paneH) {
            hoveredCategory = static_cast<std::size_t>((loc.y - titlebarH) / 28.0f);
        } else {
            hoveredCategory.reset();
        }

        if (loc.x >= catW && loc.x < catW + tplW && loc.y >= titlebarH && loc.y < titlebarH + paneH) {
            hoveredTemplate = static_cast<std::size_t>((loc.y - titlebarH) / 28.0f);
        } else {
            hoveredTemplate.reset();
        }

        const float footerY = h - footerH;
        const NSRect okRect = NSMakeRect(w - 170, footerY + 12, 75, 28);
        const NSRect cancelRect = NSMakeRect(w - 88, footerY + 12, 75, 28);
        okHovered = NSPointInRect(loc, okRect);
        cancelHovered = NSPointInRect(loc, cancelRect);
    } else if (mode == Zenvra::Platform::Cocoa::Components::PromptDialogMode::ConfirmDelete) {
        const NSRect cancelRect = NSMakeRect(w * 0.5f - 85, h - 36, 75, 26);
        const NSRect okRect = NSMakeRect(w * 0.5f + 10, h - 36, 75, 26);
        okHovered = NSPointInRect(loc, okRect);
        cancelHovered = NSPointInRect(loc, cancelRect);
    }

    if (prevOk != okHovered || prevCancel != cancelHovered) {
        [self setNeedsDisplay:YES];
    }
}

- (void)keyDown:(NSEvent *)event {
    showCaret = true;
    [self startBlinkTimer];

    NSString* chars = [event charactersIgnoringModifiers];
    NSEventModifierFlags mods = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;

    if ([chars length] == 1) {
        unichar ch = [chars characterAtIndex:0];
        if (ch == NSEnterCharacter || ch == NSNewlineCharacter || ch == NSCarriageReturnCharacter) {
            [self submit];
            return;
        } else if (ch == 0x1B) { // Escape
            [self closeWindow];
            return;
        } else if (ch == NSBackspaceCharacter || ch == NSDeleteCharacter) {
            if (caretPos > 0 && caretPos <= textValue.size()) {
                textValue.erase(caretPos - 1, 1);
                caretPos--;
                [self setNeedsDisplay:YES];
            }
            return;
        } else if (ch == NSLeftArrowFunctionKey) {
            if (caretPos > 0) {
                caretPos--;
                [self setNeedsDisplay:YES];
            }
            return;
        } else if (ch == NSRightArrowFunctionKey) {
            if (caretPos < textValue.size()) {
                caretPos++;
                [self setNeedsDisplay:YES];
            }
            return;
        } else if (ch == NSHomeFunctionKey) {
            caretPos = 0;
            [self setNeedsDisplay:YES];
            return;
        } else if (ch == NSEndFunctionKey) {
            caretPos = textValue.size();
            [self setNeedsDisplay:YES];
            return;
        }
    }

    // Command shortcuts (Paste, Select All)
    if ((mods & NSEventModifierFlagCommand) && [chars length] == 1) {
        unichar ch = [chars characterAtIndex:0];
        if (ch == 'v' || ch == 'V') {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            NSString* clip = [pb stringForType:NSPasteboardTypeString];
            if (clip && [clip length] > 0) {
                std::string clipStr = [clip UTF8String];
                if (caretPos > textValue.size()) caretPos = textValue.size();
                textValue.insert(caretPos, clipStr);
                caretPos += clipStr.size();
                [self setNeedsDisplay:YES];
            }
            return;
        } else if (ch == 'a' || ch == 'A') {
            caretPos = textValue.size();
            [self setNeedsDisplay:YES];
            return;
        }
    }

    [self interpretKeyEvents:@[event]];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    (void)replacementRange;
    NSString* str = [string isKindOfClass:[NSString class]] ? string : [(NSAttributedString*)string string];
    if ([str length] > 0) {
        std::string s = [str UTF8String];
        if (caretPos > textValue.size()) caretPos = textValue.size();
        textValue.insert(caretPos, s);
        caretPos += s.size();
        showCaret = true;
        [self startBlinkTimer];
        [self setNeedsDisplay:YES];
    }
}

- (void)doCommandBySelector:(SEL)selector {
    if (selector == @selector(deleteBackward:) || selector == @selector(deleteForward:)) {
        if (caretPos > 0 && caretPos <= textValue.size()) {
            textValue.erase(caretPos - 1, 1);
            caretPos--;
            showCaret = true;
            [self startBlinkTimer];
            [self setNeedsDisplay:YES];
        }
    } else if (selector == @selector(moveLeft:)) {
        if (caretPos > 0) {
            caretPos--;
            showCaret = true;
            [self startBlinkTimer];
            [self setNeedsDisplay:YES];
        }
    } else if (selector == @selector(moveRight:)) {
        if (caretPos < textValue.size()) {
            caretPos++;
            showCaret = true;
            [self startBlinkTimer];
            [self setNeedsDisplay:YES];
        }
    } else if (selector == @selector(moveToBeginningOfLine:) || selector == @selector(moveToBeginningOfParagraph:)) {
        caretPos = 0;
        showCaret = true;
        [self startBlinkTimer];
        [self setNeedsDisplay:YES];
    } else if (selector == @selector(moveToEndOfLine:) || selector == @selector(moveToEndOfParagraph:)) {
        caretPos = textValue.size();
        showCaret = true;
        [self startBlinkTimer];
        [self setNeedsDisplay:YES];
    } else if (selector == @selector(insertNewline:)) {
        [self submit];
    } else if (selector == @selector(cancelOperation:)) {
        [self closeWindow];
    }
}

- (BOOL)hasMarkedText { return NO; }
- (NSRange)markedRange { return NSMakeRange(NSNotFound, 0); }
- (NSRange)selectedRange { return NSMakeRange(NSNotFound, 0); }
- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange { (void)string; (void)selectedRange; (void)replacementRange; }
- (void)unmarkText {}
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText { return @[]; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange { (void)range; (void)actualRange; return nil; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point { (void)point; return 0; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange { (void)range; (void)actualRange; return NSZeroRect; }

@end

namespace Zenvra::Platform::Cocoa::Components
{

CocoaPromptDialog::CocoaPromptDialog() = default;

CocoaPromptDialog::~CocoaPromptDialog()
{
    close();
}

void CocoaPromptDialog::notify_window_closed() noexcept
{
    m_native_window = nullptr;
    m_native_view = nullptr;
    if (m_native_delegate) {
        ZDEPromptWindowDelegate* del = (ZDEPromptWindowDelegate*)m_native_delegate;
        del.owner = nullptr;
        [del release];
        m_native_delegate = nullptr;
    }
}

void CocoaPromptDialog::close() noexcept
{
    if (m_native_window) {
        NSWindow* win = (NSWindow*)m_native_window;
        m_native_window = nullptr;
        m_native_view = nullptr;
        [win setDelegate:nil];
        [win orderOut:nil];
        [win close];
    }
    if (m_native_delegate) {
        ZDEPromptWindowDelegate* del = (ZDEPromptWindowDelegate*)m_native_delegate;
        del.owner = nullptr;
        [del release];
        m_native_delegate = nullptr;
    }
}

bool CocoaPromptDialog::is_open() const noexcept
{
    if (!m_native_window) return false;
    NSWindow* win = (NSWindow*)m_native_window;
    return [win isVisible];
}

bool CocoaPromptDialog::open_new_file(
    const std::filesystem::path& target_dir,
    std::function<void(const std::string& filename, const std::string& initial_content)> on_confirm)
{
    close();

    const std::string dir_name = target_dir.empty() ? "Project" : target_dir.filename().string();
    const std::string win_title = "Add New Item - " + dir_name;

    const NSRect frame = NSMakeRect(0, 0, 780, 500);
    NSWindowStyleMask style = NSWindowStyleMaskBorderless;

    ZDEPromptWindow* window = [[ZDEPromptWindow alloc] initWithContentRect:frame
                                                                  styleMask:style
                                                                    backing:NSBackingStoreBuffered
                                                                      defer:NO];
    [window setReleasedWhenClosed:YES];
    [window setRestorable:NO];
    [window setHidesOnDeactivate:NO];
    [window setTitle:[NSString stringWithUTF8String:win_title.c_str()]];
    [window setMovableByWindowBackground:YES];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setHasShadow:YES];

    ZDEPromptWindowDelegate* del = [[ZDEPromptWindowDelegate alloc] init];
    del.owner = this;
    [window setDelegate:del];

    ZDEPromptWindowContentView* view = [[ZDEPromptWindowContentView alloc] initWithFrame:frame];
    view->owner = this;
    view->mode = PromptDialogMode::AddNewItem;
    view->title = win_title;
    view->confirmLabel = "Add";
    view->targetFolder = target_dir;
    init_templates(view->categories);
    view->selectedCategory = 0;
    view->selectedTemplate = 0;
    if (!view->categories.empty() && !view->categories[0].templates.empty()) {
        view->textValue = view->categories[0].templates[0].default_filename;
    }
    view->caretPos = view->textValue.size();
    view->onConfirmNewItem = std::move(on_confirm);

    [window setContentView:view];
    [window center];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];

    m_native_window = (void*)window;
    m_native_view = (void*)view;
    m_native_delegate = (void*)del;
    return true;
}

bool CocoaPromptDialog::open_new_folder(
    const std::filesystem::path& target_dir,
    std::function<void(const std::string&)> on_confirm)
{
    close();

    const std::string dir_name = target_dir.empty() ? "Components" : target_dir.filename().string();
    const std::string win_title = "New Folder - " + dir_name;

    const NSRect frame = NSMakeRect(0, 0, 360, 96);
    NSWindowStyleMask style = NSWindowStyleMaskBorderless;

    ZDEPromptWindow* window = [[ZDEPromptWindow alloc] initWithContentRect:frame
                                                                  styleMask:style
                                                                    backing:NSBackingStoreBuffered
                                                                      defer:NO];
    [window setReleasedWhenClosed:YES];
    [window setRestorable:NO];
    [window setHidesOnDeactivate:NO];
    [window setTitle:[NSString stringWithUTF8String:win_title.c_str()]];
    [window setMovableByWindowBackground:YES];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setHasShadow:YES];

    ZDEPromptWindowDelegate* del = [[ZDEPromptWindowDelegate alloc] init];
    del.owner = this;
    [window setDelegate:del];

    ZDEPromptWindowContentView* view = [[ZDEPromptWindowContentView alloc] initWithFrame:frame];
    view->owner = this;
    view->mode = PromptDialogMode::NewFolder;
    view->title = win_title;
    view->placeholder = "Name";
    view->confirmLabel = "Create";
    view->targetFolder = target_dir;
    view->caretPos = view->textValue.size();
    view->onConfirmString = std::move(on_confirm);

    [window setContentView:view];
    [window center];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];

    m_native_window = (void*)window;
    m_native_view = (void*)view;
    m_native_delegate = (void*)del;
    return true;
}

bool CocoaPromptDialog::open_rename(
    const std::filesystem::path& item_path,
    std::function<void(const std::string&)> on_confirm)
{
    close();

    const std::string win_title = "Rename - " + item_path.filename().string();
    const NSRect frame = NSMakeRect(0, 0, 360, 96);
    NSWindowStyleMask style = NSWindowStyleMaskBorderless;

    ZDEPromptWindow* window = [[ZDEPromptWindow alloc] initWithContentRect:frame
                                                                  styleMask:style
                                                                    backing:NSBackingStoreBuffered
                                                                      defer:NO];
    [window setReleasedWhenClosed:YES];
    [window setRestorable:NO];
    [window setHidesOnDeactivate:NO];
    [window setTitle:[NSString stringWithUTF8String:win_title.c_str()]];
    [window setMovableByWindowBackground:YES];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setHasShadow:YES];

    ZDEPromptWindowDelegate* del = [[ZDEPromptWindowDelegate alloc] init];
    del.owner = this;
    [window setDelegate:del];

    ZDEPromptWindowContentView* view = [[ZDEPromptWindowContentView alloc] initWithFrame:frame];
    view->owner = this;
    view->mode = PromptDialogMode::Rename;
    view->title = win_title;
    view->placeholder = item_path.filename().string();
    view->textValue = item_path.filename().string();
    view->caretPos = view->textValue.size();
    view->confirmLabel = "Rename";
    view->targetFolder = item_path.parent_path();
    view->onConfirmString = std::move(on_confirm);

    [window setContentView:view];
    [window center];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];

    m_native_window = (void*)window;
    m_native_view = (void*)view;
    m_native_delegate = (void*)del;
    return true;
}

bool CocoaPromptDialog::open_delete(
    const std::filesystem::path& item_path,
    std::function<void()> on_confirm)
{
    close();

    const std::string win_title = "Delete " + item_path.filename().string() + "?";
    const NSRect frame = NSMakeRect(0, 0, 360, 110);
    NSWindowStyleMask style = NSWindowStyleMaskBorderless;

    ZDEPromptWindow* window = [[ZDEPromptWindow alloc] initWithContentRect:frame
                                                                  styleMask:style
                                                                    backing:NSBackingStoreBuffered
                                                                      defer:NO];
    [window setReleasedWhenClosed:YES];
    [window setRestorable:NO];
    [window setHidesOnDeactivate:NO];
    [window setTitle:[NSString stringWithUTF8String:win_title.c_str()]];
    [window setMovableByWindowBackground:YES];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setHasShadow:YES];

    ZDEPromptWindowDelegate* del = [[ZDEPromptWindowDelegate alloc] init];
    del.owner = this;
    [window setDelegate:del];

    ZDEPromptWindowContentView* view = [[ZDEPromptWindowContentView alloc] initWithFrame:frame];
    view->owner = this;
    view->mode = PromptDialogMode::ConfirmDelete;
    view->title = win_title;
    view->subtitle = "This action cannot be undone.";
    view->confirmLabel = "Delete";
    view->targetFolder = item_path;
    view->onConfirmAction = std::move(on_confirm);

    [window setContentView:view];
    [window center];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];

    m_native_window = (void*)window;
    m_native_view = (void*)view;
    m_native_delegate = (void*)del;
    return true;
}

} // namespace Zenvra::Platform::Cocoa::Components
