/*
 * S9Wallpaper: a static lock + home screen wallpaper for the Galaxy S9 iOS port.
 *
 * On iOS 16 the wallpaper is a PosterBoard "poster", and this root's
 * first-boot LaunchServices database has none of the system poster provider
 * plug-ins (they live inside private frameworks), so PosterBoard has nothing
 * to show and both screens stay black. This tweak (ElleKit, SpringBoard only)
 * puts an image on top of SpringBoard's wallpaper window(s) (with no poster
 * only _SBWallpaperSecureWindow exists: the lock screen) and behind the home
 * screen's views (SBHomeScreenWindow), so both screens get the background.
 *
 * Defensive by design (a tweak that blocks while SpringBoard loads leaves the
 * phone on the Apple logo): nothing runs in the constructor except one
 * notification observer; all UIKit work waits until SpringBoard has finished
 * launching, the JPEG is decoded off the main thread, and the file
 * /var/mobile/Library/S9Wallpaper/disabled turns the tweak off.
 *
 * The image is /var/mobile/Library/S9Wallpaper/wallpaper.jpg (replace it and
 * respring). It is decoded once into an opaque bitmap of exactly the screen's
 * pixel size, so CoreAnimation's CPU renderer copies it 1:1 (no scaling, no
 * blending) and skips whatever is below it. What it did goes to log.txt there.
 */
#import <UIKit/UIKit.h>
#include <unistd.h>

#define DIR		"/var/mobile/Library/S9Wallpaper/"
static const NSInteger kTag = 0x53395750;	/* 'S9WP' */
static UIImage *gImage;
static BOOL gLoading;

static void wlog(NSString *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	NSString *s = [[NSString alloc] initWithFormat:fmt arguments:ap];
	va_end(ap);
	FILE *f = fopen(DIR "log.txt", "a");
	if (f != NULL) {
		fprintf(f, "%s\n", s.UTF8String);
		fclose(f);
	}
}

static UIImageView *image_view(CGRect frame)
{
	UIImageView *iv = [[UIImageView alloc] initWithImage:gImage];
	iv.tag = kTag;
	iv.frame = frame;
	iv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	iv.contentMode = UIViewContentModeScaleAspectFill;
	iv.userInteractionEnabled = NO;
	iv.opaque = YES;
	return iv;
}

static NSMutableSet *gSeen;

static void attach(void)
{
	if (gSeen == nil)
		gSeen = [NSMutableSet new];
	for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
		if (![scene isKindOfClass:UIWindowScene.class])
			continue;
		for (UIWindow *w in ((UIWindowScene *)scene).windows) {
			NSString *cls = NSStringFromClass(w.class);
			if (![gSeen containsObject:cls]) {
				[gSeen addObject:cls];
				UIView *root = w.rootViewController.view;
				wlog(@"window %@ level %.0f hidden %d root %@ bg %@", cls, w.windowLevel, w.hidden,
				    NSStringFromClass(w.rootViewController.class), root.backgroundColor);
			}
			UIView *v = [w viewWithTag:kTag];
			if ([cls rangeOfString:@"Wallpaper"].location != NSNotFound) {
				/* the wallpaper window: on top of its (empty) poster views */
				if (v == nil) {
					[w addSubview:image_view(w.bounds)];
					wlog(@"attached on top of %@", cls);
				} else if (w.subviews.lastObject != v) {
					[w bringSubviewToFront:v];
				}
			} else if ([cls isEqualToString:@"SBHomeScreenWindow"]) {
				/* the home screen: behind the icons */
				UIView *root = w.rootViewController.view;
				if (root != nil && v == nil) {
					[root insertSubview:image_view(root.bounds) atIndex:0];
					wlog(@"attached behind %@ in %@", NSStringFromClass(w.rootViewController.class), cls);
				} else if (v != nil && v.superview.subviews.firstObject != v) {
					[v.superview sendSubviewToBack:v];
				}
			}
		}
	}
}

static void apply(void)
{
	if (UIApplication.sharedApplication == nil)
		return;
	if (gImage != nil) {
		attach();
		return;
	}
	if (gLoading)
		return;
	gLoading = YES;
	UIScreen *screen = UIScreen.mainScreen;
	CGSize pts = screen.bounds.size;
	CGFloat scale = screen.scale;
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
		UIImage *src = [UIImage imageWithContentsOfFile:@DIR "wallpaper.jpg"];
		UIImage *img = nil;
		if (src != nil) {
			UIGraphicsImageRendererFormat *fmt = [UIGraphicsImageRendererFormat new];
			fmt.scale = scale;
			fmt.opaque = YES;
			UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:pts format:fmt];
			img = [r imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
				/* aspect fill, centered */
				CGSize s = src.size;
				CGFloat k = MAX(pts.width / s.width, pts.height / s.height);
				CGSize d = CGSizeMake(s.width * k, s.height * k);
				[src drawInRect:CGRectMake((pts.width - d.width) / 2, (pts.height - d.height) / 2, d.width, d.height)];
			}];
		}
		dispatch_async(dispatch_get_main_queue(), ^{
			gLoading = NO;
			if (img == nil) {
				wlog(@"no image at " DIR "wallpaper.jpg");
				return;
			}
			gImage = img;
			wlog(@"image %.0fx%.0f @%.0fx", pts.width, pts.height, scale);
			attach();
		});
	});
}

static void apply_after(double seconds)
{
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)),
	    dispatch_get_main_queue(), ^{ apply(); });
}

__attribute__((constructor)) static void init(void)
{
	if (access(DIR "disabled", F_OK) == 0)
		return;
	/* String names: no UIKit symbol is touched while SpringBoard is loading. */
	[NSNotificationCenter.defaultCenter addObserverForName:@"UIApplicationDidFinishLaunchingNotification"
	    object:nil queue:nil usingBlock:^(NSNotification *n) {
		wlog(@"SpringBoard pid %d finished launching", getpid());
		[NSNotificationCenter.defaultCenter addObserverForName:@"UIWindowDidBecomeVisibleNotification"
		    object:nil queue:nil usingBlock:^(NSNotification *m) { apply_after(0.5); }];
		for (double t = 2; t <= 64; t *= 2)
			apply_after(t);
	}];
}
