/*
 * s9play: play a test tone through iOS's normal audio path on the Galaxy S9
 * port and say where iOS routed it. Prints the audio session's current
 * output route (port type, name, UID: the S9Audio driver shows as
 * "S9 Audio (PC)" if VirtualAudio took it), then plays a 440 Hz sine,
 * 48 kHz stereo 16-bit, with an AudioQueue for the given seconds.
 *
 *   s9play [-t SECONDS] [-f HZ]
 *
 * Only plays sound; it changes no setting.
 */
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static double g_phase, g_step;

static void
fill(void *user, AudioQueueRef q, AudioQueueBufferRef b)
{
	(void)user;
	int16_t *p = (int16_t *)b->mAudioData;
	UInt32 frames = b->mAudioDataBytesCapacity / 4;
	for (UInt32 i = 0; i < frames; i++) {
		int16_t v = (int16_t)(sin(g_phase) * 8000.0);
		p[2 * i] = v;
		p[2 * i + 1] = v;
		g_phase += g_step;
		if (g_phase > 2 * M_PI) {
			g_phase -= 2 * M_PI;
		}
	}
	b->mAudioDataByteSize = frames * 4;
	AudioQueueEnqueueBuffer(q, b, 0, NULL);
}

static void
print_route(AVAudioSession *s)
{
	AVAudioSessionRouteDescription *r = s.currentRoute;
	printf("route: %lu output(s)\n", (unsigned long)r.outputs.count);
	for (AVAudioSessionPortDescription *p in r.outputs) {
		printf("  output: type %s, name \"%s\", UID %s\n", p.portType.UTF8String, p.portName.UTF8String,
		    p.UID.UTF8String);
	}
	printf("  sample rate %.0f, output volume %.2f\n", s.sampleRate, s.outputVolume);
}

int
main(int argc, char **argv)
{
	@autoreleasepool {
		double seconds = 3, hz = 440;
		int ch;
		while ((ch = getopt(argc, argv, "t:f:")) != -1) {
			switch (ch) {
			case 't': seconds = atof(optarg); break;
			case 'f': hz = atof(optarg); break;
			default:
				fprintf(stderr, "usage: s9play [-t SECONDS] [-f HZ]\n");
				return 2;
			}
		}
		AVAudioSession *s = [AVAudioSession sharedInstance];
		NSError *e = nil;
		if (![s setCategory:AVAudioSessionCategoryPlayback error:&e]) {
			printf("setCategory: %s\n", e.description.UTF8String);
		}
		if (![s setActive:YES error:&e]) {
			printf("setActive: %s\n", e.description.UTF8String);
		}
		print_route(s);

		AudioStreamBasicDescription f = {
			48000, kAudioFormatLinearPCM, kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked,
			4, 1, 4, 2, 16, 0,
		};
		g_step = 2 * M_PI * hz / 48000.0;
		AudioQueueRef q = NULL;
		OSStatus st = AudioQueueNewOutput(&f, fill, NULL, NULL, NULL, 0, &q);
		if (st != noErr) {
			printf("AudioQueueNewOutput: %d\n", (int)st);
			return 1;
		}
		for (int i = 0; i < 3; i++) {
			AudioQueueBufferRef b = NULL;
			if (AudioQueueAllocateBuffer(q, 4096 * 4, &b) == noErr) {
				fill(NULL, q, b);
			}
		}
		st = AudioQueueStart(q, NULL);
		printf("playing %.0f Hz for %.1f s: AudioQueueStart %d\n", hz, seconds, (int)st);
		fflush(stdout);
		usleep((useconds_t)(seconds * 1e6));
		print_route(s);
		AudioQueueStop(q, true);
		AudioQueueDispose(q, true);
		[s setActive:NO error:nil];
	}
	return 0;
}
