#include <gtest/gtest.h>
#include <cstdlib>
#include "video/convert.h"
#include "video/image.h"
#include "video/imageset.h"

using namespace evx;

class ConvertTest : public ::testing::Test {
protected:
    static constexpr int WIDTH = 32;
    static constexpr int HEIGHT = 32;

    image rgb_src, rgb_dst;
    image_set yuv;

    void SetUp() override {
        create_image(EVX_IMAGE_FORMAT_R8G8B8, WIDTH, HEIGHT, &rgb_src);
        create_image(EVX_IMAGE_FORMAT_R8G8B8, WIDTH, HEIGHT, &rgb_dst);
        yuv.initialize(EVX_IMAGE_FORMAT_R16S, WIDTH, HEIGHT);
    }

    void TearDown() override {
        destroy_image(&rgb_src);
        destroy_image(&rgb_dst);
    }
};

TEST_F(ConvertTest, RGBToYUVToRGBRoundtrip) {
    // Fill with a gradient pattern
    uint8 *data = rgb_src.query_data();
    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        int offset = j * rgb_src.query_row_pitch() + i * 3;
        data[offset + 0] = (i * 8) & 0xFF;  // R
        data[offset + 1] = (j * 8) & 0xFF;  // G
        data[offset + 2] = ((i + j) * 4) & 0xFF;  // B
    }

    EXPECT_EQ(convert_image(rgb_src, &yuv), EVX_SUCCESS);
    EXPECT_EQ(convert_image(yuv, &rgb_dst), EVX_SUCCESS);

    // Check roundtrip accuracy - allow some error from YUV conversion
    uint8 *src_data = rgb_src.query_data();
    uint8 *dst_data = rgb_dst.query_data();
    int max_error = 0;
    double total_error = 0;
    int pixel_count = 0;

    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        int offset = j * rgb_src.query_row_pitch() + i * 3;
        for (int c = 0; c < 3; c++) {
            int err = std::abs((int)src_data[offset + c] - (int)dst_data[offset + c]);
            if (err > max_error) max_error = err;
            total_error += err;
            pixel_count++;
        }
    }

    double avg_error = total_error / pixel_count;
    // YUV 4:2:0 conversion loses some chroma information
    EXPECT_LT(max_error, 10) << "Max per-channel error should be small";
    EXPECT_LT(avg_error, 3.0) << "Average error should be very small";
}

TEST_F(ConvertTest, BlackImageRoundtrip) {
    // Black (all zeros) - should convert perfectly
    memset(rgb_src.query_data(), 0, rgb_src.query_slice_pitch());

    EXPECT_EQ(convert_image(rgb_src, &yuv), EVX_SUCCESS);
    EXPECT_EQ(convert_image(yuv, &rgb_dst), EVX_SUCCESS);

    uint8 *dst_data = rgb_dst.query_data();
    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        int offset = j * rgb_dst.query_row_pitch() + i * 3;
        for (int c = 0; c < 3; c++) {
            EXPECT_NEAR(dst_data[offset + c], 0, 2)
                << "Black pixel at (" << i << "," << j << ") channel " << c;
        }
    }
}

TEST_F(ConvertTest, WhiteImageRoundtrip) {
    memset(rgb_src.query_data(), 255, rgb_src.query_slice_pitch());

    EXPECT_EQ(convert_image(rgb_src, &yuv), EVX_SUCCESS);
    EXPECT_EQ(convert_image(yuv, &rgb_dst), EVX_SUCCESS);

    uint8 *dst_data = rgb_dst.query_data();
    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        int offset = j * rgb_dst.query_row_pitch() + i * 3;
        for (int c = 0; c < 3; c++) {
            EXPECT_NEAR(dst_data[offset + c], 255, 3)
                << "White pixel at (" << i << "," << j << ") channel " << c;
        }
    }
}

TEST_F(ConvertTest, PureRedRoundtrip) {
    uint8 *data = rgb_src.query_data();
    for (int j = 0; j < HEIGHT; j++)
    for (int i = 0; i < WIDTH; i++) {
        int offset = j * rgb_src.query_row_pitch() + i * 3;
        data[offset + 0] = 255;  // R
        data[offset + 1] = 0;    // G
        data[offset + 2] = 0;    // B
    }

    EXPECT_EQ(convert_image(rgb_src, &yuv), EVX_SUCCESS);
    EXPECT_EQ(convert_image(yuv, &rgb_dst), EVX_SUCCESS);

    uint8 *dst_data = rgb_dst.query_data();
    // Check center pixel to avoid chroma subsampling edge effects
    int ci = WIDTH / 2, cj = HEIGHT / 2;
    int offset = cj * rgb_dst.query_row_pitch() + ci * 3;
    EXPECT_NEAR(dst_data[offset + 0], 255, 5) << "Red channel";
    EXPECT_NEAR(dst_data[offset + 1], 0, 10) << "Green channel";
    EXPECT_NEAR(dst_data[offset + 2], 0, 10) << "Blue channel";
}

TEST_F(ConvertTest, YUVPlanesHaveCorrectDimensions) {
    EXPECT_EQ(convert_image(rgb_src, &yuv), EVX_SUCCESS);

    // Y plane should be full resolution
    EXPECT_EQ(yuv.query_y_image()->query_width(), (uint32)WIDTH);
    EXPECT_EQ(yuv.query_y_image()->query_height(), (uint32)HEIGHT);

    // U and V planes should be half resolution (4:2:0)
    EXPECT_EQ(yuv.query_u_image()->query_width(), (uint32)(WIDTH / 2));
    EXPECT_EQ(yuv.query_u_image()->query_height(), (uint32)(HEIGHT / 2));
    EXPECT_EQ(yuv.query_v_image()->query_width(), (uint32)(WIDTH / 2));
    EXPECT_EQ(yuv.query_v_image()->query_height(), (uint32)(HEIGHT / 2));
}
