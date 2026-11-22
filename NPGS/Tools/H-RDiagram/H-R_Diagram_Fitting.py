"""Extract H-R diagram curves from an image into CSV coordinates."""

import argparse
import csv
from pathlib import Path

DEFAULT_INPUT = Path(__file__).resolve().parent / "H-R_Diagram_Curve.png"
DEFAULT_OUTPUT = (
    Path(__file__).resolve().parents[3]
    / "local/generated/hr-diagram/all_curves_coordinates.csv"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT,
                        help="input image (default: H-R_Diagram_Curve.png beside this script)")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help="output CSV (default: repo/local/generated/hr-diagram/)")
    args = parser.parse_args()

    import cv2
    import numpy as np

    # 加载图像
    image_path = args.input.expanduser()
    image = cv2.imread(str(image_path))
    if image is None:
        raise FileNotFoundError(f"无法加载图像文件: {image_path}")

    # 转换为HSV图像
    hsv_image = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)

    # 图像尺寸
    img_height, img_width = image.shape[:2]

    color_ranges = {
        'Ia': ([85, 100, 100], [95, 255, 255]),   # 青色
        'Ib': ([170, 100, 100], [180, 255, 255]), # 红色
        'II': ([50, 100, 100], [70, 255, 255]),   # 绿色
        'III': ([25, 100, 100], [35, 255, 255]),  # 黄色
        'IV': ([125, 100, 100], [150, 255, 255]), # 紫色
        'V':  ([0, 0, 255], [180, 0, 255])        # 白色
    }

    # 转换像素坐标为实际坐标的函数
    def pixel_to_actual(x, y):
        b_v = -0.3 + (x / 22.0) * 0.1
        luminosity = 10 ** (-5 + (img_height - y) / 63.4)
        return b_v, luminosity

    # 存储所有曲线的坐标点
    all_coordinates = {}

    # 遍历每条曲线，提取坐标点并存储到字典中
    for curve, (lower, upper) in color_ranges.items():
        lower = np.array(lower, dtype="uint8")
        upper = np.array(upper, dtype="uint8")

        # 创建掩膜
        mask = cv2.inRange(hsv_image, lower, upper)
        coordinates = []

        # 遍历图像的每一列，每隔5个像素取一个点
        for x in range(0, img_width, 5):
            for y in range(img_height):
                if mask[y, x] > 0:
                    coordinates.append((y, x))
                    break  # 确保同一列只取一个点

        if not coordinates:
            print(f"没有找到{curve}曲线的像素")
        else:
            all_coordinates[curve] = [pixel_to_actual(x, y) for y, x in coordinates]

    # 将所有曲线的坐标点输出到一个CSV文件中
    output_csv_path = args.output.expanduser()
    output_csv_path.parent.mkdir(parents=True, exist_ok=True)
    with output_csv_path.open("w", newline='', encoding="utf-8") as csvfile:
        csv_writer = csv.writer(csvfile)
        header = ["B-V"] + list(color_ranges.keys())
        csv_writer.writerow(header)

        # 找到所有B-V色指数的集合
        all_b_v = sorted(set(b_v for coords in all_coordinates.values() for b_v, _ in coords))

        for b_v in all_b_v:
            row = [b_v]
            for curve in color_ranges.keys():
                luminosity = next((lum for b_v_val, lum in all_coordinates.get(curve, []) if b_v_val == b_v), -1)
                row.append(luminosity)
            csv_writer.writerow(row)

    print(f"所有曲线的坐标点已输出到文件: {output_csv_path}")


if __name__ == "__main__":
    main()
