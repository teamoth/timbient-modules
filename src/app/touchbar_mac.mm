// The Touch Bar (Macs from 2016 to 2022 that have one). SDL doesn't know about it, so this attaches an NSTouchBar to the
// app's window: the window is the last stop when the system asks the responders on screen for their Touch Bar.
// Everything here runs on the main thread (Cocoa calls the buttons and sliders from SDL's event pumping).
#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <cmath>
#include "touchbar.h"

using namespace tb;

static TouchBarHooks gHooks;
static TouchBarState gShown;
static bool gHave = false, gFirst = true;

static NSString* const kBlend = @"com.timbient.modules.blend";
static NSString* const kPause = @"com.timbient.modules.pause";
static NSString* const kNow = @"com.timbient.modules.now";
static NSString* const kCancel = @"com.timbient.modules.cancel";
static NSString* const kBlendTo = @"com.timbient.modules.blendto";
static NSString* const kLevel = @"com.timbient.modules.level";

@interface TimbientTouchBar : NSObject <NSTouchBarDelegate> {
@public
  NSTouchBar* bar;
  NSSliderTouchBarItem* blend;
  NSSliderTouchBarItem* level;
  NSButton* pause;
  double blendTouched, levelTouched;   // when a finger last moved them (the app doesn't move them back meanwhile)
}
@end

@implementation TimbientTouchBar
- (NSButton*)button:(NSString*)title action:(SEL)action {
  NSButton* b = [NSButton buttonWithTitle:title target:self action:action];
  return b;
}
- (NSTouchBarItem*)touchBar:(NSTouchBar*)touchBar makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
  if ([identifier isEqualToString:kBlend] || [identifier isEqualToString:kLevel]) {
    const bool isBlend = [identifier isEqualToString:kBlend];
    NSSliderTouchBarItem* it = [[NSSliderTouchBarItem alloc] initWithIdentifier:identifier];
    it.label = isBlend ? @"BLEND" : @"LEVEL";
    it.slider.minValue = 0;
    it.slider.maxValue = 1;
    it.slider.continuous = YES;
    it.slider.doubleValue = isBlend ? gShown.at : gShown.level;
    [it.slider.widthAnchor constraintGreaterThanOrEqualToConstant:(isBlend ? 380 : 240)].active = YES;
    it.target = self;
    it.action = isBlend ? @selector(blendMoved:) : @selector(levelMoved:);
    if (isBlend) blend = it; else level = it;
    return it;
  }
  NSCustomTouchBarItem* it = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
  if ([identifier isEqualToString:kPause]) { pause = [self button:(gShown.paused ? @"GO" : @"PAUSE") action:@selector(pauseTapped:)]; it.view = pause; }
  else if ([identifier isEqualToString:kNow]) it.view = [self button:@"NOW" action:@selector(nowTapped:)];
  else if ([identifier isEqualToString:kCancel]) it.view = [self button:@"CANCEL" action:@selector(cancelTapped:)];
  else if ([identifier isEqualToString:kBlendTo]) it.view = [self button:@"BLEND TO..." action:@selector(blendToTapped:)];
  else return nil;
  return it;
}
- (void)blendMoved:(id)sender { blendTouched = CFAbsoluteTimeGetCurrent(); if (gHooks.blendSet && blend) gHooks.blendSet((float)blend.slider.doubleValue); }
- (void)levelMoved:(id)sender { levelTouched = CFAbsoluteTimeGetCurrent(); if (gHooks.levelSet && level) gHooks.levelSet((float)level.slider.doubleValue); }
- (void)pauseTapped:(id)sender { if (gHooks.pause) gHooks.pause(); }
- (void)nowTapped:(id)sender { if (gHooks.now) gHooks.now(); }
- (void)cancelTapped:(id)sender { if (gHooks.cancel) gHooks.cancel(); }
- (void)blendToTapped:(id)sender { if (gHooks.blendTo) gHooks.blendTo(); }
@end

static TimbientTouchBar* gBar = nil;
static NSWindow* gWin = nil;

// A new layout is a new NSTouchBar handed to the window: macOS shows that straight away, where changing the items of the bar
// already showing could wait until something (a click in the window) made it look again
static void setLayout(bool blending) {
  if (!gBar || !gWin) return;
  NSTouchBar* b = [[NSTouchBar alloc] init];
  b.delegate = gBar;
  b.defaultItemIdentifiers = blending ? @[kBlend, kPause, kNow, kCancel] : @[kBlendTo, NSTouchBarItemIdentifierFlexibleSpace, kLevel];
  gBar->blend = nil; gBar->level = nil; gBar->pause = nil;   // (the new bar makes its own items, from what's shown now)
  NSTouchBar* old = gBar->bar; gBar->bar = b; gWin.touchBar = b;
#if !__has_feature(objc_arc)
  [old release];   // (the window keeps its own hold on the bar it shows)
#endif
  (void)old;
}

namespace tb {

void touchBarInstall(SDL_Window* window, const TouchBarHooks& hooks) {
  if (!window || gBar) return;
  if (![NSWindow instancesRespondToSelector:@selector(setTouchBar:)]) return;   // (macOS before 10.12.2: no Touch Bar at all)
  NSWindow* w = (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
  if (!w) return;
  gHooks = hooks;
  gBar = [[TimbientTouchBar alloc] init];
  gWin = w;
  setLayout(false);
  gHave = true;
}

void touchBarUpdate(const TouchBarState& s) {   // (what's shown is remembered even before an item exists: it's made with it)
  if (!gHave || !gBar) return;
  const double now = CFAbsoluteTimeGetCurrent();
  if (gFirst || s.blending != gShown.blending) { gShown.blending = s.blending; gShown.at = s.at; gShown.paused = s.paused; gShown.level = s.level; if (!gFirst) setLayout(s.blending); }
  if (std::fabs(s.at - gShown.at) > 0.002f && now - gBar->blendTouched > 0.4) { gShown.at = s.at; if (gBar->blend) gBar->blend.slider.doubleValue = s.at; }
  if (std::fabs(s.level - gShown.level) > 0.002f && now - gBar->levelTouched > 0.4) { gShown.level = s.level; if (gBar->level) gBar->level.slider.doubleValue = s.level; }
  if (gFirst || s.paused != gShown.paused) { gShown.paused = s.paused; if (gBar->pause) gBar->pause.title = s.paused ? @"GO" : @"PAUSE"; }
  gFirst = false;
}

}  // namespace tb
