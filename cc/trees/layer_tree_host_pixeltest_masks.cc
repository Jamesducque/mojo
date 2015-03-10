// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "cc/layers/content_layer_client.h"
#include "cc/layers/picture_image_layer.h"
#include "cc/layers/picture_layer.h"
#include "cc/layers/solid_color_layer.h"
#include "cc/test/layer_tree_pixel_resource_test.h"
#include "cc/test/pixel_comparator.h"

#if !defined(OS_ANDROID)

namespace cc {
namespace {

typedef ParameterizedPixelResourceTest LayerTreeHostMasksPixelTest;

INSTANTIATE_PIXEL_RESOURCE_TEST_CASE_P(LayerTreeHostMasksPixelTest);

class MaskContentLayerClient : public ContentLayerClient {
 public:
  explicit MaskContentLayerClient(const gfx::Size& bounds) : bounds_(bounds) {}
  ~MaskContentLayerClient() override {}

  bool FillsBoundsCompletely() const override { return false; }

  void PaintContents(SkCanvas* canvas,
                     const gfx::Rect& rect,
                     PaintingControlSetting picture_control) override {
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(SkIntToScalar(2));
    paint.setColor(SK_ColorWHITE);

    canvas->clear(SK_ColorTRANSPARENT);
    gfx::Rect inset_rect(bounds_);
    while (!inset_rect.IsEmpty()) {
      inset_rect.Inset(3, 3, 2, 2);
      canvas->drawRect(
          SkRect::MakeXYWH(inset_rect.x(), inset_rect.y(),
                           inset_rect.width(), inset_rect.height()),
          paint);
      inset_rect.Inset(3, 3, 2, 2);
    }
  }

  scoped_refptr<DisplayItemList> PaintContentsToDisplayList(
      const gfx::Rect& clip,
      PaintingControlSetting picture_control) override {
    NOTIMPLEMENTED();
    return DisplayItemList::Create();
  }

 private:
  gfx::Size bounds_;
};

// TODO(enne): these time out on Windows.  http://crbug.com/435632
#if !defined(OS_WIN)

TEST_P(LayerTreeHostMasksPixelTest, MaskOfLayer) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green = CreateSolidColorLayerWithBorder(
      gfx::Rect(50, 50, 100, 100), kCSSGreen, 1, SK_ColorBLACK);
  background->AddChild(green);

  gfx::Size mask_bounds(100, 100);
  MaskContentLayerClient client(mask_bounds);
  scoped_refptr<PictureLayer> mask = PictureLayer::Create(&client);
  mask->SetBounds(mask_bounds);
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);
  green->SetMaskLayer(mask.get());

  RunPixelResourceTest(background,
                       base::FilePath(FILE_PATH_LITERAL("mask_of_layer.png")));
}

TEST_P(LayerTreeHostMasksPixelTest, ImageMaskOfLayer) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(200, 200), SK_ColorWHITE);

  gfx::Size mask_bounds(100, 100);

  scoped_refptr<PictureImageLayer> mask = PictureImageLayer::Create();
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);
  mask->SetBounds(mask_bounds);

  SkBitmap bitmap;
  bitmap.allocN32Pixels(400, 400);
  SkCanvas canvas(bitmap);
  canvas.scale(SkIntToScalar(4), SkIntToScalar(4));
  MaskContentLayerClient client(mask_bounds);
  client.PaintContents(&canvas, gfx::Rect(mask_bounds),
                       ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  mask->SetBitmap(bitmap);

  scoped_refptr<SolidColorLayer> green = CreateSolidColorLayerWithBorder(
      gfx::Rect(50, 50, 100, 100), kCSSGreen, 1, SK_ColorBLACK);
  green->SetMaskLayer(mask.get());
  background->AddChild(green);

  RunPixelResourceTest(
      background, base::FilePath(FILE_PATH_LITERAL("image_mask_of_layer.png")));
}

TEST_P(LayerTreeHostMasksPixelTest, MaskOfClippedLayer) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(200, 200), SK_ColorWHITE);

  // Clip to the top half of the green layer.
  scoped_refptr<Layer> clip = Layer::Create();
  clip->SetPosition(gfx::Point(0, 0));
  clip->SetBounds(gfx::Size(200, 100));
  clip->SetMasksToBounds(true);
  background->AddChild(clip);

  scoped_refptr<SolidColorLayer> green = CreateSolidColorLayerWithBorder(
      gfx::Rect(50, 50, 100, 100), kCSSGreen, 1, SK_ColorBLACK);
  clip->AddChild(green);

  gfx::Size mask_bounds(100, 100);
  MaskContentLayerClient client(mask_bounds);
  scoped_refptr<PictureLayer> mask = PictureLayer::Create(&client);
  mask->SetBounds(mask_bounds);
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);
  green->SetMaskLayer(mask.get());

  RunPixelResourceTest(
      background,
      base::FilePath(FILE_PATH_LITERAL("mask_of_clipped_layer.png")));
}

TEST_P(LayerTreeHostMasksPixelTest, MaskWithReplica) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(200, 200), SK_ColorWHITE);

  gfx::Size mask_bounds(100, 100);
  MaskContentLayerClient client(mask_bounds);
  scoped_refptr<PictureLayer> mask = PictureLayer::Create(&client);
  mask->SetBounds(mask_bounds);
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);

  scoped_refptr<SolidColorLayer> green = CreateSolidColorLayerWithBorder(
      gfx::Rect(0, 0, 100, 100), kCSSGreen, 1, SK_ColorBLACK);
  background->AddChild(green);
  green->SetMaskLayer(mask.get());

  gfx::Transform replica_transform;
  replica_transform.Rotate(-90.0);

  scoped_refptr<Layer> replica = Layer::Create();
  replica->SetTransformOrigin(gfx::Point3F(50.f, 50.f, 0.f));
  replica->SetPosition(gfx::Point(100, 100));
  replica->SetTransform(replica_transform);
  green->SetReplicaLayer(replica.get());

  RunPixelResourceTest(
      background, base::FilePath(FILE_PATH_LITERAL("mask_with_replica.png")));
}

TEST_P(LayerTreeHostMasksPixelTest, MaskWithReplicaOfClippedLayer) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(200, 200), SK_ColorWHITE);

  gfx::Size mask_bounds(100, 100);
  MaskContentLayerClient client(mask_bounds);
  scoped_refptr<PictureLayer> mask = PictureLayer::Create(&client);
  mask->SetBounds(mask_bounds);
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);

  // Clip to the bottom half of the green layer, and the left half of the
  // replica.
  scoped_refptr<Layer> clip = Layer::Create();
  clip->SetPosition(gfx::Point(0, 50));
  clip->SetBounds(gfx::Size(150, 150));
  clip->SetMasksToBounds(true);
  background->AddChild(clip);

  scoped_refptr<SolidColorLayer> green = CreateSolidColorLayerWithBorder(
      gfx::Rect(0, -50, 100, 100), kCSSGreen, 1, SK_ColorBLACK);
  clip->AddChild(green);
  green->SetMaskLayer(mask.get());

  gfx::Transform replica_transform;
  replica_transform.Rotate(-90.0);

  scoped_refptr<Layer> replica = Layer::Create();
  replica->SetTransformOrigin(gfx::Point3F(50.f, 50.f, 0.f));
  replica->SetPosition(gfx::Point(100, 100));
  replica->SetTransform(replica_transform);
  green->SetReplicaLayer(replica.get());

  RunPixelResourceTest(background,
                       base::FilePath(FILE_PATH_LITERAL(
                           "mask_with_replica_of_clipped_layer.png")));
}

TEST_P(LayerTreeHostMasksPixelTest, MaskOfReplica) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(200, 200), SK_ColorWHITE);

  gfx::Size mask_bounds(100, 100);
  MaskContentLayerClient client(mask_bounds);
  scoped_refptr<PictureLayer> mask = PictureLayer::Create(&client);
  mask->SetBounds(mask_bounds);
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);

  scoped_refptr<SolidColorLayer> green = CreateSolidColorLayerWithBorder(
      gfx::Rect(50, 0, 100, 100), kCSSGreen, 1, SK_ColorBLACK);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> orange = CreateSolidColorLayer(
      gfx::Rect(-50, 50, 50, 50), kCSSOrange);
  green->AddChild(orange);

  gfx::Transform replica_transform;
  replica_transform.Rotate(180.0);
  replica_transform.Translate(100.0, 0.0);

  scoped_refptr<Layer> replica = Layer::Create();
  replica->SetTransformOrigin(gfx::Point3F(100.f, 100.f, 0.f));
  replica->SetPosition(gfx::Point());
  replica->SetTransform(replica_transform);
  replica->SetMaskLayer(mask.get());
  green->SetReplicaLayer(replica.get());

  RunPixelResourceTest(
      background, base::FilePath(FILE_PATH_LITERAL("mask_of_replica.png")));
}

TEST_P(LayerTreeHostMasksPixelTest, MaskOfReplicaOfClippedLayer) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(200, 200), SK_ColorWHITE);

  gfx::Size mask_bounds(100, 100);
  MaskContentLayerClient client(mask_bounds);
  scoped_refptr<PictureLayer> mask = PictureLayer::Create(&client);
  mask->SetBounds(mask_bounds);
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);

  // Clip to the bottom 3/4 of the green layer, and the top 3/4 of the replica.
  scoped_refptr<Layer> clip = Layer::Create();
  clip->SetPosition(gfx::Point(0, 25));
  clip->SetBounds(gfx::Size(200, 150));
  clip->SetMasksToBounds(true);
  background->AddChild(clip);

  scoped_refptr<SolidColorLayer> green = CreateSolidColorLayerWithBorder(
      gfx::Rect(50, -25, 100, 100), kCSSGreen, 1, SK_ColorBLACK);
  clip->AddChild(green);

  scoped_refptr<SolidColorLayer> orange = CreateSolidColorLayer(
      gfx::Rect(-50, 50, 50, 50), kCSSOrange);
  green->AddChild(orange);

  gfx::Transform replica_transform;
  replica_transform.Rotate(180.0);
  replica_transform.Translate(100.0, 0.0);

  scoped_refptr<Layer> replica = Layer::Create();
  replica->SetTransformOrigin(gfx::Point3F(100.f, 100.f, 0.f));
  replica->SetPosition(gfx::Point());
  replica->SetTransform(replica_transform);
  replica->SetMaskLayer(mask.get());
  green->SetReplicaLayer(replica.get());

  RunPixelResourceTest(background,
                       base::FilePath(FILE_PATH_LITERAL(
                           "mask_of_replica_of_clipped_layer.png")));
}

#endif  // !defined(OS_WIN)

class CheckerContentLayerClient : public ContentLayerClient {
 public:
  CheckerContentLayerClient(const gfx::Size& bounds,
                           SkColor color,
                           bool vertical)
      : bounds_(bounds), color_(color), vertical_(vertical) {}
  ~CheckerContentLayerClient() override {}
  bool FillsBoundsCompletely() const override { return false; }
  void PaintContents(SkCanvas* canvas,
                     const gfx::Rect& rect,
                     PaintingControlSetting picture_control) override {
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(SkIntToScalar(4));
    paint.setColor(color_);
    canvas->clear(SK_ColorTRANSPARENT);
    if (vertical_) {
      for (int i = 4; i < bounds_.width(); i += 16) {
        canvas->drawLine(i, 0, i, bounds_.height(), paint);
      }
    } else {
      for (int i = 4; i < bounds_.height(); i += 16) {
        canvas->drawLine(0, i, bounds_.width(), i, paint);
      }
    }
  }
  scoped_refptr<DisplayItemList> PaintContentsToDisplayList(
      const gfx::Rect& clip,
      PaintingControlSetting picture_control) override {
    NOTIMPLEMENTED();
    return DisplayItemList::Create();
  }

 private:
  gfx::Size bounds_;
  SkColor color_;
  bool vertical_;
};

class CircleContentLayerClient : public ContentLayerClient {
 public:
  explicit CircleContentLayerClient(const gfx::Size& bounds)
      : bounds_(bounds) {}
  ~CircleContentLayerClient() override {}
  bool FillsBoundsCompletely() const override { return false; }
  void PaintContents(SkCanvas* canvas,
                     const gfx::Rect& rect,
                     PaintingControlSetting picture_control) override {
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(SK_ColorWHITE);
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->drawCircle(bounds_.width() / 2,
                       bounds_.height() / 2,
                       bounds_.width() / 4,
                       paint);
  }
  scoped_refptr<DisplayItemList> PaintContentsToDisplayList(
      const gfx::Rect& clip,
      PaintingControlSetting picture_control) override {
    NOTIMPLEMENTED();
    return DisplayItemList::Create();
  }

 private:
  gfx::Size bounds_;
};

using LayerTreeHostMasksForBackgroundFiltersPixelTest =
    ParameterizedPixelResourceTest;

INSTANTIATE_TEST_CASE_P(
    PixelResourceTest,
    LayerTreeHostMasksForBackgroundFiltersPixelTest,
    ::testing::Values(
        // SOFTWARE, Background filters aren't implemented in software
        GL_GPU_RASTER_2D_DRAW,
        GL_ONE_COPY_2D_STAGING_2D_DRAW,
        GL_ONE_COPY_RECT_STAGING_2D_DRAW,
        GL_ONE_COPY_EXTERNAL_STAGING_2D_DRAW,
        GL_ZERO_COPY_2D_DRAW,
        GL_ZERO_COPY_RECT_DRAW,
        GL_ZERO_COPY_EXTERNAL_DRAW,
        GL_ASYNC_UPLOAD_2D_DRAW));

TEST_P(LayerTreeHostMasksForBackgroundFiltersPixelTest,
       MaskOfLayerWithBackgroundFilter) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(256, 256), SK_ColorWHITE);

  gfx::Size picture_bounds(256, 256);
  CheckerContentLayerClient picture_client(picture_bounds, SK_ColorGREEN, true);
  scoped_refptr<PictureLayer> picture = PictureLayer::Create(&picture_client);
  picture->SetBounds(picture_bounds);
  picture->SetIsDrawable(true);

  scoped_refptr<SolidColorLayer> blur = CreateSolidColorLayer(
      gfx::Rect(256, 256), SK_ColorTRANSPARENT);
  background->AddChild(picture);
  background->AddChild(blur);

  FilterOperations filters;
  filters.Append(FilterOperation::CreateBlurFilter(2.f));
  blur->SetBackgroundFilters(filters);

  gfx::Size mask_bounds(256, 256);
  CircleContentLayerClient mask_client(mask_bounds);
  scoped_refptr<PictureLayer> mask = PictureLayer::Create(&mask_client);
  mask->SetBounds(mask_bounds);
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);
  blur->SetMaskLayer(mask.get());

  float percentage_pixels_large_error = 2.5f;  // 2.5%, ~1600px / (256*256)
  float percentage_pixels_small_error = 0.0f;
  float average_error_allowed_in_bad_pixels = 60.0f;
  int large_error_allowed = 100;
  int small_error_allowed = 0;
  pixel_comparator_.reset(new FuzzyPixelComparator(
      true,  // discard_alpha
      percentage_pixels_large_error,
      percentage_pixels_small_error,
      average_error_allowed_in_bad_pixels,
      large_error_allowed,
      small_error_allowed));

  RunPixelResourceTest(background,
                       base::FilePath(
                           FILE_PATH_LITERAL("mask_of_background_filter.png")));
}

TEST_P(LayerTreeHostMasksForBackgroundFiltersPixelTest,
       MaskOfLayerWithBlend) {
  scoped_refptr<SolidColorLayer> background = CreateSolidColorLayer(
      gfx::Rect(256, 256), SK_ColorWHITE);

  gfx::Size picture_bounds(256, 256);
  CheckerContentLayerClient picture_client_vertical(
      picture_bounds, SK_ColorGREEN, true);
  scoped_refptr<PictureLayer> picture_vertical =
      PictureLayer::Create(&picture_client_vertical);
  picture_vertical->SetBounds(picture_bounds);
  picture_vertical->SetIsDrawable(true);

  CheckerContentLayerClient picture_client_horizontal(
      picture_bounds, SK_ColorMAGENTA, false);
  scoped_refptr<PictureLayer> picture_horizontal =
      PictureLayer::Create(&picture_client_horizontal);
  picture_horizontal->SetBounds(picture_bounds);
  picture_horizontal->SetIsDrawable(true);
  picture_horizontal->SetContentsOpaque(false);
  picture_horizontal->SetBlendMode(SkXfermode::kMultiply_Mode);

  background->AddChild(picture_vertical);
  background->AddChild(picture_horizontal);

  gfx::Size mask_bounds(256, 256);
  CircleContentLayerClient mask_client(mask_bounds);
  scoped_refptr<PictureLayer> mask = PictureLayer::Create(&mask_client);
  mask->SetBounds(mask_bounds);
  mask->SetIsDrawable(true);
  mask->SetIsMask(true);
  picture_horizontal->SetMaskLayer(mask.get());

  float percentage_pixels_large_error = 0.01f;  // 0.01%, ~6px / (256*256)
  float percentage_pixels_small_error = 0.0f;
  float average_error_allowed_in_bad_pixels = 256.0f;
  int large_error_allowed = 256;
  int small_error_allowed = 0;
  pixel_comparator_.reset(new FuzzyPixelComparator(
      true,  // discard_alpha
      percentage_pixels_large_error,
      percentage_pixels_small_error,
      average_error_allowed_in_bad_pixels,
      large_error_allowed,
      small_error_allowed));

  RunPixelResourceTest(background,
                       base::FilePath(
                           FILE_PATH_LITERAL("mask_of_layer_with_blend.png")));
}

}  // namespace
}  // namespace cc

#endif  // OS_ANDROID
