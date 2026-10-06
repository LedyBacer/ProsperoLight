#!/usr/bin/env python3
# ps5-native-app-boilerplate - Launcher localization and layout.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compose concise ProsperoLight translations from authored terminology.
Existing Moonlight translations take precedence. Catalog JSON remains the
editable source of truth; this script is a one-time bootstrap, not a build step.
"""
import json,re
from pathlib import Path
root=Path(__file__).resolve().parents[2];directory=root/'assets/locales';base=json.loads((directory/'en.json').read_text())
terms={}
for path in [Path(__file__).with_name('phrases.tsv'),Path(__file__).with_name('common.tsv')]:
 rows=path.read_text().splitlines();keys=rows[0][2:].split('|')
 for row in rows[1:]:
  language,values=row.split('\t',1);values=values.split('|');assert len(keys)==len(values)
  terms.setdefault(language,{}).update(zip(keys,values))
# Meaning-equivalent compact UI copy. Technical identifiers remain unchanged.
templates={
'About ProsperoLight':'{About} ProsperoLight', 'Address of the PC':'{PCs}: {Address}',
'An app':'{Apps}', 'An app is running':'{Apps}: {Start}', 'An app is running on this PC':'{PCs} / {Apps}: {Start}',
'App stopped':'{Stop app}: {Done}', 'Apps on this PC':'{PCs} / {Apps}',
'Asking Sunshine for a pairing request.':'Sunshine: {Pairing} / PIN', 'Asking the PC for its apps':'{Preparing}: {PCs} / {Apps}',
'By address, with a port if needed':'{Address} / {Port}', 'Change port':'{Port} / {Settings}',
'Choose':'{Start}', 'Choose and change':'{Settings}', 'Choose a PC first, or add one by its address.':'{PCs} / {Add a PC}: {Address}',
'Choose H.264 in Settings, or change the encoder on the PC.':'{Settings}: H.264 / {PCs}: {Video}',
'Choose a supported chroma/HDR profile or configure Vibepollo on the PC.':'{Settings}: {Chroma sampling} / HDR; Vibepollo: {Settings}',
'Choose the PC in PCs, then type the PIN shown here into Sunshine.':'{PCs} → Sunshine: PIN',
'Close':'{Back}', 'Codec':'{Video codec}', 'Connecting':'{Preparing}: {Network}', 'Connection':'{Network}',
'Could not change the port of this PC':'{Save failed}: {PCs} / {Port}', 'Could not confirm the stop':'{Warning}: {Stop app}',
'Could not remove this PC':'{Warning}: {Remove}', 'Could not save frame pacing':'{Save failed}: {Frame pacing}',
'Could not save host app quit':'{Save failed}: {Stop app}', 'Could not save logging':'{Save failed}: {Diagnostic logs}',
'Could not save menu sounds':'{Save failed}: {Menu sounds}', 'Could not save language':'{Save failed}: {Language}',
'Credits and first steps':'{Thanks} / {Start}', 'Decoder':'{Decoder load}', 'Decoder load recommendation':'{Decoder load}',
'Display':'{Picture size}', 'Downloading':'{Preparing}', 'Enter this PIN on ':'PIN → ', 'Files on this PS5':'PS5: {Files}',
'Finishing':'{Done}', 'For example 192.168.1.50, or 192.168.1.50:48989':'{Address}: 192.168.1.50 / 192.168.1.50:48989',
'Found ':'{PCs}: ', 'Found a new PC':'{PCs}: {Ready}', 'Frosted glass':'ProsperoLight', 'Getting started':'{Start}',
'Hold':'L1 / R1', 'Hold to remove':'{Remove}', 'Important:':'{Warning}:', 'Keyboard':'{Keyboard}',
'Language changes immediately. Automatic follows the console language.':'{Language}: {Automatic (console language)} / {Settings}',
'Local network':'LAN / {Network}', 'Logs':'{Diagnostic logs}', 'Menu navigation and confirmation sounds.':'{Menu sounds}',
'Menu sound effects made with ElevenLabs.':'{Menu sounds}: ElevenLabs', 'No PC found on this network':'{Network} / {PCs}: {None}',
'No PC selected':'{PCs}: {None}', 'No PC to pair':'{Pairing}: {PCs} / {None}', 'No PCs yet':'{PCs}: {None}',
'No apps on this PC':'{PCs} / {Apps}: {None}', 'No measured decoder limit for this profile.':'{Decoder load}: —',
'Not answering':'{Offline}', 'Not paired yet':'{Unpaired}', 'Note:':'{Tip}:', 'Nothing':'{None}',
'Nothing is running on this PC':'{PCs} / {Apps}: {None}', 'Online and paired':'{Online} / {Pairing}: {Done}',
'Online, not paired':'{Online} / {Unpaired}', 'Open':'{Start}', 'Open games':'{Games}',
'Open Sunshine on the PC, choose PIN, and type the code.':'Sunshine → PIN',
'Open it again when the console says it was updated.':'{Updating}: {Done} → {Start}', 'PS5 edition':'PS5',
'Pair again to use this PC.':'{Try again}: {Pair}', 'Pair it once with a PIN to see its apps.':'{Pair}: PIN → {Apps}',
'Pair once':'{Pair}', 'Pair this PC':'{Pair}: {PCs}', 'Pair to use':'{Pair}', 'Paired with ':'{Pairing}: ',
'Paired with this PS5':'PS5 / {Pairing}: {Done}', 'Pairing did not start':'{Warning}: {Pairing}',
'Pairing failed':'{Warning}: {Pairing}', 'Pairing required':'{Pairing}', 'Pick an app in Games and start it.':'{Games} → {Apps} → {Start}',
'Pipeline':'{Classic} / {Adaptive (experimental)}', 'Play':'{Start}', 'Port of ':'{Port}: ',
'Powered by Moonlight':'Moonlight', 'Preparing a PIN':'{Preparing}: PIN', 'Project credits':'{Thanks}',
'ProsperoLight closes now.':'ProsperoLight: {Stop app}',
'ProsperoLight is an unofficial PS5 client brought to you by BlackBearReloaded.':'ProsperoLight (PS5) — BlackBearReloaded',
'ProsperoLight keeps trying. Check that Sunshine is running on the PC.':'{Try again}. Sunshine / {PCs}: {Online}',
'ProsperoLight speaks the Moonlight protocol through moonlight-common-c. All credit for it goes to the Moonlight developers and contributors.':'{Thanks}: Moonlight / moonlight-common-c',
"ProsperoLight's night blue under frosted panels":'ProsperoLight',
'PyroWave needs a compatible host, high bitrate and wired LAN.':'PyroWave: Vibepollo / {High bitrate} / {Wired LAN}',
'PyroWave profile unavailable':'PyroWave: {Unsupported}', 'This PyroWave profile is unavailable':'PyroWave: {Unsupported}',
'Quit host app after stream':'{Leave stream} → {Stop app} ({PCs})', 'Ready to start':'{Ready}',
'Reconnecting':'{Try again}: {Network}', 'Requested bitrate':'{Bitrate}', 'Resume stream':'{Start}',
'Run Sunshine on the PC':'Sunshine / {PCs}: {Start}', 'Running now':'{Start}', 'Running on the PC':'{PCs}: {Start}',
'Searching this network for Sunshine':'{Search again}: Sunshine / {Network}', 'Select':'{Start}',
'Settings were not saved':'{Save failed}: {Settings}', 'Skip':'{Back}', 'Something went wrong.':'{Warning}',
'Sound':'{Audio}', 'Start stream':'{Start}', 'Starting the update helper':'{Preparing}: {Updating}',
'Stereo, 48 kHz':'{Stereo}, 48 kHz', '5.1 surround, 48 kHz':'5.1 / 48 kHz', '5.1 surround':'5.1',
'Stop it before unpairing.':'{Stop app} → {Unpair}', 'Stop it on the device that started it, then search again.':'{Stop app} → {Search again}',
'Stop the app on the device that started it, or in Sunshine.':'{Stop app}: Sunshine / {PCs}',
'Stop the game or app on the PC when leaving the stream.':'{Leave stream} → {Stop app} ({PCs})',
'Stopping the app':'{Stop app}', 'Stream from your PC':'{PCs} → PS5', 'Sunshine PC':'Sunshine / {PCs}',
'Sunshine did not answer':'Sunshine: {Offline}', 'Sunshine is not answering':'Sunshine: {Offline}',
'Sunshine is reconnecting':'Sunshine: {Try again}', 'Sunshine port':'Sunshine: {Port}', 'Sunshine ready':'Sunshine: {Ready}',
'Sunshine refused the request.':'Sunshine: {Warning}', 'Sunshine rejected the request.':'Sunshine: {Warning}',
'Sunshine returned an empty list. Add apps in Sunshine, then try again.':'Sunshine / {Apps}: {None}. {Try again}',
'Sunshine still has this PS5 in its list.':'Sunshine: PS5 / {Pairing}', "Sunshine's default is 47989":'Sunshine: 47989',
'TV output':'TV / {Resolution}', 'The PC':'{PCs}', 'The PC is not answering':'{PCs}: {Offline}',
'The PC is offline.':'{PCs}: {Offline}', 'The PC list was not saved.':'{Save failed}: {PCs}',
'The PC must be online before pairing.':'{Pairing}: {PCs} / {Online}', 'The PC must be online to unpair.':'{Unpair}: {PCs} / {Online}',
'The PIN was not accepted in time.':'PIN: {Warning}. {Try again}', 'The app did not stop':'{Stop app}: {Warning}',
'The console refused the file.':'PS5 / {Files}: {Warning}', 'The list is full. Remove a PC first.':'{Remove} → {Add a PC}',
'The rest is on the app\'s page on homebrew.page.':'homebrew.page', 'The stream ended':'{Leave stream}: {Done}',
'The update could not start.':'{Updating}: {Warning}', 'The update helper did not answer.':'{Updating}: {Offline}',
'The update was not installed':'{Updating}: {Warning}', 'This PC cannot encode HDR':'{PCs}: HDR / {Unsupported}',
'This PC cannot encode HEVC':'{PCs}: HEVC / {Unsupported}', 'This PC is not paired':'{PCs}: {Unpaired}',
'This PS5 is not paired':'PS5: {Unpaired}', 'This PS5 will need a new PIN to use it again.':'PS5: {Pairing} / PIN',
'This closes by itself when Sunshine accepts the PIN.':'Sunshine: PIN → {Done}', 'Tip:':'{Tip}:',
'Try again in a moment.':'{Try again}', 'Try again.':'{Try again}',
'Turn HDR off in Settings, or enable HEVC Main10 on the PC.':'{Settings}: HDR ✕ / {PCs}: HEVC Main10',
'Type a port from 1 to 65535':'{Port}: 1–65535',
'Type an address such as 192.168.1.50, or 192.168.1.50:48989':'{Address}: 192.168.1.50 / 192.168.1.50:48989',
'Type its address, for example 192.168.1.50. Add a port after a colon if Sunshine does not use 47989: 192.168.1.50:48989.':'{Address}: 192.168.1.50; {Port}: 47989 → 192.168.1.50:48989',
"Type the PC's address":'{PCs}: {Address}', 'Unknown':'—', 'Unpacking':'{Preparing}: {Files}',
'Unpair ':'{Unpair}: ', 'Unpairing':'{Unpair}', 'Unpairing failed':'{Unpair}: {Warning}',
'Update available':'{Updating}: {Ready}', 'Updating':'{Updating}', 'Version ':'{Version}: ',
'Warning:':'{Warning}:', "What's new":'{Updating}', 'Your PCs':'{PCs}', 'ends the stream and returns here':'{Leave stream}',
'left':'…', 'this PC':'{PCs}', 'the PC':'{PCs}', ' is not answering':': {Offline}', ' is out':': {Ready}',
' is out. Get it from homebrew.page.':': {Ready} — homebrew.page', ' new PCs':' · {PCs}', ' removed':': {Remove}',
'%u available':'%u · {Apps}', '%u saved':'%u · {PCs}', "%u (Sunshine's default)":'%u (Sunshine)',
'%s does not advertise it. The stream will not start.':'%s: {Unsupported}',
'\nProsperoLight is as it was.':'\nProsperoLight: {Cancel}',
'.\nUpdate now downloads and checks it. ProsperoLight then closes while the new version is put in place.':'.\n{Preparing} → {Checking} → {Stop app} → {Updating}',
'%.1f of %.1f MB%s%s':'%.1f / %.1f MB%s%s',
'48 kHz Opus, decoded on the console.':'Opus / 48 kHz / PS5',
'4:4:4 is available with PyroWave; native codecs use 4:2:0.':'PyroWave: 4:4:4 / H.264, HEVC: 4:2:0',
'90 and 120 FPS use the 119.88 Hz output mode.':'90 / 120 FPS → 119.88 Hz',
'An update or a reinstall does not touch them.':'{Updating}: {Files} ✓',
'Bounded logs and output interval traces saved after the stream.':'{Leave stream} → {Diagnostic logs} / CSV (32,768)',
'Cancel. Nothing is changed until the download is checked.':'{Cancel}. {Preparing} → {Checking} → {Updating}',
'Classic decodes one frame at a time. Adaptive overlaps frames when decoding falls behind.':'{Classic}: 1; {Adaptive (experimental)}: 1+ ({Decoder load})',
'Cores reserved for decoding; the stream uses the rest.':'CPU: {Decoder load} / {Host session}',
'Keep it open, on the same network as this PS5.':'Sunshine / PS5: {Network} / LAN',
"Kept in the app's own storage: no filesystem access at start-up.":'PS5 / app: {Files} ({Warning})',
'Off shows each frame at once: lower latency, visible tearing.':'V-Sync ✕: {Unpaced} / {Warning}',
'PCs running Sunshine on this network appear by themselves. Add one by hand when it is on another network, or when discovery is blocked.':'Sunshine: {Network} → {PCs}. {Add a PC}: {Address} / {Port}',
'Smooth frame timing; VRR uses fixed refresh if unavailable.':'{Paced} + VRR ({Unsupported}: {Paced})',
'Smooth up to %.0f Mbps at this frame rate.':'{Bitrate} ≤ %.0f Mbps / {Frame rate}',
'Smooth up to %.0f Mbps. Above %.0f Mbps the picture freezes about once a second.':'{Bitrate} ≤ %.0f Mbps. > %.0f Mbps: {Stuttering} (~1 s)',
'Smooth up to 500 Mbps. Above 500 Mbps stability may decrease; above 700 Mbps packet loss is more likely. Use wired LAN.':'≤500 Mbps ✓; 500–700 Mbps: {Warning}; >700 Mbps: {Packet loss}. {Wired LAN}',
'Smooth up to 500 Mbps. Wired LAN recommended.':'≤500 Mbps ✓. {Wired LAN}',
'Smooth up to 80 Mbps at 4K60. Above 80 Mbps the picture may stutter.':'4K60: ≤80 Mbps ✓; >80 Mbps: {Stuttering}',
'Smooth up to 80 Mbps at this frame rate.':'≤80 Mbps ✓ / {Frame rate}',
'H.264 4K120 can drop frames at any bitrate. Lower the frame rate or use HEVC/PyroWave.':'H.264 4K120: {Stuttering} ({Bitrate}: 1–1000 Mbps). {Frame rate} ↓ / HEVC / PyroWave',
'H.264 4K90 may drop frames at any bitrate.':'H.264 4K90: {Stuttering} ({Bitrate}: 1–1000 Mbps)',
'HDR10 through HEVC Main10 or 10-bit PyroWave, when advertised by the PC.':'HDR10: HEVC Main10 / PyroWave 10-bit ({PCs})',
'Higher is not always better: the decoder sets the limit.':'{High bitrate} → {Decoder load}',
'Start Sunshine on a PC on this network and it appears here, or add it by its address.':'Sunshine / {PCs}: {Start}; {Add a PC}: {Address}',
'Start Sunshine on the PC, or add it by its address.':'Sunshine: {Start}; {Add a PC}: {Address}',
'TV safe keeps a margin for televisions that crop the picture.':'TV: {TV safe} / {Edge to edge}',
'Thanks to the Sunshine developers for the host on the PC, to the whole PS5 homebrew community, and to every developer whose tools and libraries make ProsperoLight possible.':'{Thanks}: Sunshine / PS5 homebrew / ProsperoLight',
'The picture Sunshine encodes. 1440p is scaled to the 4K output.':'Sunshine / {Resolution}: 1440p → 4K',
}
for language,glossary in terms.items():
 path=directory/(language+'.json');catalog=json.loads(path.read_text())
 def term(key):return glossary.get(key,catalog.get(key,key))
 for source in base:
  if catalog.get(source,source)!=source:continue
  if source in glossary:catalog[source]=glossary[source]
  elif source in templates:catalog[source]=re.sub(r'\{([^}]+)\}',lambda m:term(m[1]),templates[source])
  elif source=='Paced+VRR':catalog[source]=term('Paced')+' + VRR'
 catalog.update({})
 path.write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+'\n')
 remaining=[k for k,v in catalog.items() if k==v and len(k)>8 and not re.fullmatch(r'[%su\s·0-9FPMbpsK-]+',k) and k not in ['ProsperoLight ','  ·  HDR','V-Sync']]
 if remaining:print(language,remaining)
