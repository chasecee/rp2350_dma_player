cd /gif-converter
source .venv/bin/activate
pip install pillow imageio
pip install "imageio[ffmpeg]"
python convert.py --source source-mp4 --output output --size 140 --rotate -90
