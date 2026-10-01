# HOME banner audio

`BannerAudio.wav` is a 3-second excerpt of `intro.v2.mixed.wav`, supplied by Rhlp-Engineering.
The 7.15-second original is preserved outside the repository.

Conversion: take 0.65–3.65 seconds (skip the leading near-silence), apply a 15 ms fade-in and 250 ms fade-out, resample to 16 kHz stereo PCM16. No speed or pitch change.

```sh
ffmpeg -ss 0.65 -i intro.v2.mixed.wav -t 3 \
  -af 'afade=t=in:st=0:d=0.015,afade=t=out:st=2.75:d=0.25' \
  -ar 16000 -ac 2 -c:a pcm_s16le -map_metadata -1 BannerAudio.wav
```

The HOME banner requires two channels and at most three seconds of audio:
https://3dbrew.org/wiki/CBMD#BCWAV

Run `bash tests/test_cia_banner_audio.sh` after replacing the asset.
