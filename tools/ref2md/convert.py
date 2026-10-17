"""
@file  convert.py
@brief 把 reference/ 下的 PDF 批量转换为同目录、同名的 Markdown

用法（在仓库根目录执行）：
    uv venv --python 3.12 tools/ref2md/.venv
    uv pip install --python tools/ref2md/.venv/bin/python -r tools/ref2md/requirements.txt
    tools/ref2md/.venv/bin/python tools/ref2md/convert.py            # 只转换新增或更新过的 PDF
    tools/ref2md/.venv/bin/python tools/ref2md/convert.py --force    # 全部重新转换
    tools/ref2md/.venv/bin/python tools/ref2md/convert.py a.pdf b.pdf  # 只转换指定文件

输出约定：
- 每页开头插入 `<!-- p.N -->` 页码标记，便于引用和回 PDF 核对。
- 图片放在 PDF 同目录的 `images/` 下，Markdown 以相对路径引用：
  - 竖版文档（论文、数据手册）：只导出版面分析识别出的图片区域；
  - 横版文档（讲稿）：框图常由矢量文本框拼成，识别不出图片区域，所以每页导出一张整页图，
    放在该页标记之后。
- 文件开头注明来源与转换工具版本。
"""

import argparse
import datetime
import importlib.metadata
import io
import re
import shutil
import sys
from pathlib import Path

import pypdfium2 as pdfium
from docling.datamodel.base_models import InputFormat
from docling.datamodel.pipeline_options import PdfPipelineOptions
from docling.document_converter import DocumentConverter, PdfFormatOption
from docling_core.types.doc import ImageRefMode

REPO_ROOT = Path(__file__).resolve().parents[2]
REFERENCE_DIR = REPO_ROOT / "reference"
PAGE_BREAK = "<!-- docling-page-break -->"
IMAGES_DIR = "images"
# 图片渲染倍率：1.0 对应 72 DPI，2.0 对应 144 DPI，框图里的小字也能看清
IMAGES_SCALE = 2.0
# 讲稿整页图：JPEG 不到 PNG 体积的 60% 时才改用 JPEG
JPEG_QUALITY = 90
JPEG_SIZE_RATIO = 0.6

# 部分 PDF 用 Wingdings 字体画项目符号，提取后会残留成字母 l（一级）和 o（二级）
BULLET_L1 = re.compile(r"^- l ", re.MULTILINE)
BULLET_L2 = re.compile(r"^- o ", re.MULTILINE)
BLANK_LINES = re.compile(r"\n{3,}")


def build_converter():
    options = PdfPipelineOptions()
    # 所有资料都带文本层，关闭 OCR 可以避免把图中文字当正文混进来
    options.do_ocr = False
    options.do_table_structure = True
    options.generate_picture_images = True
    options.images_scale = IMAGES_SCALE
    return DocumentConverter(
        format_options={InputFormat.PDF: PdfFormatOption(pipeline_options=options)}
    )


def is_slide_deck(pdf_path):
    pdf = pdfium.PdfDocument(pdf_path)
    try:
        width, height = pdf[0].get_size()
    finally:
        pdf.close()
    return width > height


def save_page_image(image, images_dir, stem):
    # 纯框图的页面用 PNG 更小也更清晰；带照片的页面 PNG 会很大，此时改用 JPEG
    png = io.BytesIO()
    image.save(png, "PNG", optimize=True)
    jpg = io.BytesIO()
    image.convert("RGB").save(jpg, "JPEG", quality=JPEG_QUALITY)
    if jpg.tell() < png.tell() * JPEG_SIZE_RATIO:
        name, data = f"{stem}.jpg", jpg
    else:
        name, data = f"{stem}.png", png
    (images_dir / name).write_bytes(data.getvalue())
    return name


def render_pages(pdf_path, images_dir):
    pdf = pdfium.PdfDocument(pdf_path)
    names = []
    try:
        for i in range(len(pdf)):
            image = pdf[i].render(scale=IMAGES_SCALE).to_pil()
            names.append(save_page_image(image, images_dir, f"page-{i + 1:03d}"))
    finally:
        pdf.close()
    return names


def to_markdown(doc, pdf_path, md_path):
    images_dir = md_path.parent / IMAGES_DIR
    if images_dir.exists():
        shutil.rmtree(images_dir)  # 清掉上一次转换留下的旧图片

    if is_slide_deck(pdf_path):
        images_dir.mkdir()
        page_images = render_pages(pdf_path, images_dir)
        body = doc.export_to_markdown(
            page_break_placeholder=PAGE_BREAK, image_placeholder=""
        )
    else:
        page_images = []
        # save_as_markdown 负责把图片写入 images/ 并生成相对路径引用，之后再读回做后处理
        doc.save_as_markdown(
            md_path,
            artifacts_dir=Path(IMAGES_DIR),
            image_mode=ImageRefMode.REFERENCED,
            page_break_placeholder=PAGE_BREAK,
        )
        body = md_path.read_text(encoding="utf-8")
        if images_dir.exists() and not any(images_dir.iterdir()):
            images_dir.rmdir()  # 文档里没有图片时不留空目录

    parts = []
    for i, page in enumerate(body.split(PAGE_BREAK), 1):
        part = f"<!-- p.{i} -->\n\n"
        if i <= len(page_images):
            part += f"![第 {i} 页]({IMAGES_DIR}/{page_images[i - 1]})\n\n"
        parts.append(part + page.strip())
    body = "\n\n".join(parts)
    body = BULLET_L1.sub("- ", body)
    body = BULLET_L2.sub("  - ", body)
    return BLANK_LINES.sub("\n\n", body), bool(page_images)


def header(pdf_path, slide_deck):
    version = importlib.metadata.version("docling")
    today = datetime.date.today().isoformat()
    images = "每页的整页图" if slide_deck else "图片"
    return (
        f"> 本文件由 `{pdf_path.name}` 自动转换（docling {version}，{today}）。"
        f"{images}在 `{IMAGES_DIR}/` 目录；表格与数字请以 PDF 原文为准；"
        "`<!-- p.N -->` 标记对应 PDF 第 N 页。\n\n"
    )


def needs_update(pdf_path, md_path):
    return not md_path.exists() or md_path.stat().st_mtime < pdf_path.stat().st_mtime


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("pdfs", nargs="*", type=Path, help="只转换这些 PDF")
    parser.add_argument("--force", action="store_true", help="忽略时间戳，全部重新转换")
    args = parser.parse_args()

    pdfs = [p.resolve() for p in args.pdfs] or sorted(REFERENCE_DIR.rglob("*.pdf"))
    todo = [p for p in pdfs if args.force or needs_update(p, p.with_suffix(".md"))]
    print(f"共 {len(pdfs)} 个 PDF，需要转换 {len(todo)} 个")

    converter = build_converter()
    failed = []
    for i, pdf in enumerate(todo, 1):
        md = pdf.with_suffix(".md")
        print(f"[{i}/{len(todo)}] {pdf.relative_to(REPO_ROOT)}", flush=True)
        try:
            doc = converter.convert(pdf).document
            body, slide_deck = to_markdown(doc, pdf, md)
        except Exception as exc:  # 单个文件失败不影响其余文件
            print(f"    失败：{exc}", flush=True)
            failed.append(pdf)
            continue
        md.write_text(header(pdf, slide_deck) + body + "\n", encoding="utf-8")

    if failed:
        print("以下文件转换失败：")
        for pdf in failed:
            print(f"  {pdf.relative_to(REPO_ROOT)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
