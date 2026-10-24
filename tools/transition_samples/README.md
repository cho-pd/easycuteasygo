# 전환 미리보기 샘플 (VEDIT)

조박사님이 고른 두 장(`img/t1.png`, `img/t2.png`)을 256px JPEG로 줄인 것.
모든 전환의 `transitions/*/preview_strip.png`는 이 두 장으로 만든다:

    build/Release/transitionthumbs.exe --transitions transitions --base-a tools/transition_samples/t1.jpg --base-b tools/transition_samples/t2.jpg --colors 64

(PATH에 Qt·FFmpeg·vcpkg bin이 있어야 한다. 128px 칸 12장, 64색 PNG.)
