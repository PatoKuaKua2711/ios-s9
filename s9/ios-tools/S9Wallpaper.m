/*
 * S9Wallpaper: a static lock + home screen wallpaper for the Galaxy S9 iOS port.
 *
 * On iOS 16 the wallpaper is a PosterBoard "poster", and this root's
 * first-boot LaunchServices database has none of the system poster provider
 * plug-ins (they live inside private frameworks), so PosterBoard has nothing
 * to show and both screens stay black. This tweak (ElleKit, SpringBoard only)
 * puts an image on top of SpringBoard's wallpaper window(s): those windows sit
 * below every other window, so it is the background of the lock screen, the
 * home screen and the app switcher.
 *
 * The image is /var/mobile/Library/S9Wallpaper/wallpaper.jpg (replace it and
 * respring). It is decoded once into an opaque bitmap of exactly the screen's
 * pixel size, so CoreAnimation's CPU renderer copies it 1:1 (no scaling, no
 * blending) and skips whatever is below it.
 */
#import <UIKit/UIKit.h>

static NSString *const kPath = @"/var/mobile/Library/S9Wallpaper/wallpaper.jpg";
static const NSInteger kTag = 0x53395750;	/* 'S9WP' */
static UIImage *gImage;

static UIImage *load_image(void)
{
	UIImage *src = [UIImage imageWithContentsOfFile:kPath];
	if (src == nil)
		return nil;
	UIScreen *screen = UIScreen.mainScreen;
	CGSize pts = screen.bounds.size;
	UIGraphicsImageRendererFormat *fmt = [UIGraphicsImageRendererFormat preferredFormat];
	fmt.scale = screen.scale;
	fmt.opaque = YES;
	UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:pts format:fmt];
	return [r imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
		/* aspect fill, centered */
		CGSize s = src.size;
		CGFloat k = MAX(pts.width / s.width, pts.height / s.height);
		CGSize d = CGSizeMake(s.width * k, s.height * k);
		[src drawInRect:CGRectMake((pts.width - d.width) / 2, (pts.height - d.height) / 2, d.width, d.height)];
	}];
}

static void apply(void)
{
	if (gImage == nil)
		gImage = load_image();
	if (gImage == nil)
		return;
	for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
		if (![scene isKindOfClass:UIWindowScene.class])
			continue;
		for (UIWindow *w in ((UIWindowScene *)scene).windows) {
			if ([NSStringFromClass(w.class) rangeOfString:@"Wallpaper"].location == NSNotFound)
				continue;
			UIView *v = [w viewWithTag:kTag];
			if (v == nil) {
				UIImageView *iv = [[UIImageView alloc] initWithImage:gImage];
				iv.tag = kTag;
				iv.frame = w.bounds;
				iv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
				iv.contentMode = UIViewContentModeScaleAspectFill;
				iv.userInteractionEnabled = NO;
				iv.opaque = YES;
				iv.layer.contentsScale = UIScreen.mainScreen.scale;
				[w addSubview:iv];
				NSLog(@"S9Wallpaper: on %@", NSStringFromClass(w.class));
			} else if (w.subviews.lastObject != v) {
				[w bringSubviewToFront:v];
			}
		}
	}
}

__attribute__((constructor)) static void init(void)
{
	/* New windows (the lock screen's, after a respring...) and late subviews. */
	[NSNotificationCenter.defaultCenter addObserverForName:UIWindowDidBecomeVisibleNotification
	    object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { apply(); }];
	for (double t = 1; t <= 64; t *= 2)
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(t * NSEC_PER_SEC)),
		    dispatch_get_main_queue(), ^{ apply(); });
}
