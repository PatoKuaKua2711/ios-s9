#!/usr/bin/env python3
"""Remove daemons useless on the Galaxy S9 iOS port from a launchd.plist.

  trim-daemons.py LAUNCHD_PLIST [--apply]

Without --apply it only lists what it would remove. The S9 has no working
cellular, iCloud account, sensors, Bluetooth/Wi-Fi or GPU, so dozens of Apple
daemons only crash-loop and eat RAM -- which was starving the system (2 GB,
~28 MB free) and making jetsam kill Sileo/NewTerm/WebContent on launch. This
drops the useless ones (keeping all boot/UI/app-launch infrastructure), which
is what makes the jailbreak actually usable. Run from tools/prep-ios6.sh on
the mounted image; also applied live to a running root (then reboot).
"""
import plistlib, sys
path = sys.argv[1]
apply = len(sys.argv) > 2 and sys.argv[2] == "--apply"
d = plistlib.load(open(path, 'rb'))
ld = d['LaunchDaemons']

# Remove daemons useless on this device (no cellular, no account, no sensors,
# no BT/wifi, no GPU). Matched as substrings of the label (com.apple.*).
REMOVE = [
 "voicemail","callservices","telephonyutilities","CommCenter","cellmon",
 "identityservices","imagent","imtransfer","IMDPersistence","imavagent",
 "facetime","avconferenced",
 "rapportd","sharingd",".sharing.","airdrop","screensharing","InternetSharing",
 "icloud","protectedcloud","keychainsharing","cloudkeychain","itunescloud",
 "findmydevice",".fmfd",".fmflocatord","searchparty","icloudsubscription",
 "cloudd","cloudpaird","cloudphotod","cloudsettings","cloudconfigurationd",
 "assistant",".siri","siriknowledge","siriinference","corespeech","sirittsd",
 "spotlight","searchd","corespotlight","suggestd","parsec","proactive",
 "health","homed","homeenergy","HomeKit",
 "remindd","chronod","nfcd","nfc.","locationd","mobileasset","duet","coreidvd",
 "accessoryupdater","remotepairing","mediaanalysis","photoanalysis","photolibrary",
 "newsd","stocks","weatherd","tipsd","gamed","gamecenter","familycircle",
 "familynotification","screentime","followupd","triald",
 "passd","wallet","appstore","softwareupdate","storedownload","storeaccount",
 "videosubscriptions","bluetoothd","bluetooth","fairplayd","wifivelocity",
 "wifianalytics","mapspushd","navd","itunesstored",
]
# Never remove these (critical infrastructure), even if they match above.
KEEP = [
 "backboard","SpringBoard","mediaserverd","mediaremote","runningboard","cfprefs",
 "trustd","amfid","securityd","installd",".lsd","container","usermanager",
 "ios-s9","s9net","openssh","sshd","UserEventAgent","powerd","dasd","notifyd",
 "distnoted","logd","syslog","keybagd","mobileactivationd","aggregated",
 "frontboard","assertiond","watchdog","thermal","springboardservices",
 "backboardd","mobilegestalt","launchservicesd","diskarbitrationd","configd",
 "networkd","mDNSResponder","symptomsd","nsurlsessiond",
]

def keep(label):
    return any(k in label for k in KEEP)
def junk(label):
    return any(r in label for r in REMOVE)

removed = []
for pathkey, job in list(ld.items()):
    if not isinstance(job, dict): continue
    label = job.get('Label','')
    if not label.startswith('com.apple.'): continue
    if keep(label): continue
    if junk(label):
        removed.append(label)
        if apply: del ld[pathkey]

removed.sort()
print(f"REMOVER {len(removed)} daemons (de {len(ld)+ (len(removed) if apply else 0)}):")
for l in removed: print("  ", l)
if apply:
    plistlib.dump(d, open(path,'wb'), fmt=plistlib.FMT_BINARY)
    print(f"\nAPLICADO. LaunchDaemons agora = {len(ld)}")
