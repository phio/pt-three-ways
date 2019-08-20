#pragma once

struct RenderParams {
  int width{1920};
  int height{1080};
  bool preview{false};
  int samplesPerPixel{40};
  int maxCpus{1};
};
