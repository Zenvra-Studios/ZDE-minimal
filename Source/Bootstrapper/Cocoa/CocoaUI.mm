#include "../NativeUI.h"
#include <iostream>

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#endif

void ShowNativeErrorDialog(const std::string& title, const std::string& message) {
#ifdef __OBJC__
    NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
    NSString* nsMessage = [NSString stringWithUTF8String:message.c_str()];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:nsTitle];
        [alert setInformativeText:nsMessage];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    });
#else
    std::cerr << "ERROR: " << title << "\n" << message << std::endl;
#endif
}

void PrintDiagnostic(const std::string& message) {
    std::cout << message << std::endl;
}
