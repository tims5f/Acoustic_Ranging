"""Extract a silent, palette-optimized README GIF using FFmpeg.

python tools/make_demo_gif.py input.mp4 --start 10 --duration 8
FFmpeg can be on PATH, supplied with --ffmpeg, or from imageio-ffmpeg.
"""
import argparse
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('video', type=Path)
    parser.add_argument('--start', type=float, default=0)
    parser.add_argument('--duration', type=float, default=8)
    parser.add_argument('--width', type=int, default=720)
    parser.add_argument('--fps', type=int, default=10)
    parser.add_argument('--rotate', choices=['none', 'cw', 'ccw'], default='none')
    parser.add_argument('--output', type=Path, default=Path('docs/assets/demo.gif'))
    parser.add_argument('--ffmpeg')
    args = parser.parse_args()
    if not args.video.is_file():
        parser.error('Video file does not exist')
    if args.start < 0 or args.duration <= 0 or args.width < 16 or args.fps < 1:
        parser.error('Invalid start/duration/width/fps')
    ffmpeg = args.ffmpeg or shutil.which('ffmpeg')
    if not ffmpeg:
        try:
            import imageio_ffmpeg
            ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
        except ImportError:
            parser.error('Install FFmpeg or run: python -m pip install imageio-ffmpeg')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    rotation = {'none': '', 'cw': 'transpose=1,', 'ccw': 'transpose=2,'}[args.rotate]
    filters = (
        rotation + f'fps={args.fps},scale={args.width}:-2:flags=lanczos,split[a][b];'
        '[a]palettegen=max_colors=96:stats_mode=diff[p];'
        '[b][p]paletteuse=dither=none:diff_mode=rectangle'
    )
    subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y',
                    '-ss', str(args.start), '-t', str(args.duration),
                    '-i', str(args.video), '-an', '-filter_complex', filters,
                    '-loop', '0', str(args.output)], check=True)
    print(f'{args.output}: {args.output.stat().st_size / 1024**2:.2f} MiB')


if __name__ == '__main__':
    main()
