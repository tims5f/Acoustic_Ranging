"""Build the defense guide from the reviewed Markdown source."""
from pathlib import Path
import re
from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'output' / 'defense'
OUT.mkdir(parents=True, exist_ok=True)
SRC = ROOT / 'docs' / '分布式声波测距系统答辩说明.md'
DEST = OUT / '分布式声波测距系统答辩说明.docx'

def diagram():
    im=Image.new('RGB',(1800,930),'white'); draw=ImageDraw.Draw(im)
    font=lambda n:ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',n)
    def box(rect,title,lines):
        draw.rounded_rectangle(rect,18,fill='#F1F5F8',outline='#5F7180',width=3)
        x,y,x2,y2=rect
        draw.text((x+25,y+20),title,font=font(39),fill='black')
        for i,line in enumerate(lines):draw.text((x+25,y+83+i*54),line,font=font(33),fill='#253747')
    def arrow(a,b):
        import math
        draw.line([a,b],fill='#506878',width=4)
        angle=math.atan2(b[1]-a[1],b[0]-a[0]); r=16
        draw.polygon([b,(b[0]-r*math.cos(angle-.5),b[1]-r*math.sin(angle-.5)),(b[0]-r*math.cos(angle+.5),b[1]-r*math.sin(angle+.5))],fill='#506878')
    draw.text((530,12),'外部声源 手机测试音或拍手',font=font(40),fill='black')
    arrow((760,77),(360,150)); arrow((1020,77),(1440,150))
    box((20,155,675,510),'A 端 MASTER',['连续采音与事件缓存','统一时基中的采样时间','接收 B 波形并计算相关','距离 方向 结果与 ACK 管理'])
    box((1125,155,1780,510),'B 端 SLAVE',['连续采音与事件缓存','本地时间映射到 A 时基','发送带时间戳的波形','接收结果 显示并确认'])
    box((730,265,1070,410),'交换机',['双向 UDP 通信'])
    arrow((675,323),(730,323));arrow((730,355),(675,355))
    arrow((1070,323),(1125,323));arrow((1125,355),(1070,355))
    box((20,560,1780,760),'共享控制与显示',['DHT11 自动温度或手动温度 → 统一声速    CLAP / PHONE → 双板确认后生效','LCD 显示距离 声源侧 TOA 时差变化量 波形与诊断状态'])
    draw.text((40,795),'底层支撑  HAL 与 BSP 外设驱动    LwIP 网络    DMA 与定时器中断',font=font(35),fill='black')
    draw.text((40,860),'同一固件自动选举角色    正常测距在板端完成    ST-Link 仅用于开发诊断',font=font(34),fill='#253747')
    path=OUT/'framework.png';im.save(path);return path

d=Document();sec=d.sections[0]
sec.page_width=Inches(8.5);sec.page_height=Inches(11)
sec.top_margin=Inches(.75);sec.bottom_margin=Inches(.7)
sec.left_margin=Inches(.82);sec.right_margin=Inches(.82)
sec.header_distance=Inches(.3);sec.footer_distance=Inches(.3)
for name in ['Normal','Title','Subtitle','Heading 1','Heading 2','Heading 3','Header','Footer']:
    s=d.styles[name];s.font.name='Microsoft YaHei';s.font.color.rgb=RGBColor(0,0,0)
    s._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'),'Microsoft YaHei')
    ppr=s._element.get_or_add_pPr()
    for border in list(ppr.findall(qn('w:pBdr'))):ppr.remove(border)
normal=d.styles['Normal'];normal.font.size=Pt(11)
normal.paragraph_format.line_spacing=1.25
normal.paragraph_format.space_after=Pt(6)
normal.paragraph_format.widow_control=True
for name,size,before,after in [('Title',23,0,12),('Heading 1',16,16,8),('Heading 2',12.5,10,5)]:
    s=d.styles[name];s.font.size=Pt(size);s.font.bold=True
    s.paragraph_format.space_before=Pt(before);s.paragraph_format.space_after=Pt(after)
    s.paragraph_format.keep_with_next=True
header=sec.header.paragraphs[0];header.text='分布式声波测距系统  答辩学习说明'
header.runs[0].font.size=Pt(8)
footer=sec.footer.paragraphs[0];footer.alignment=WD_ALIGN_PARAGRAPH.CENTER
footer.add_run('2026 年 11 月 5 日  ·  ')
field=OxmlElement('w:fldSimple');field.set(qn('w:instr'),'PAGE');footer._p.append(field)
for r in footer.runs:r.font.size=Pt(8)

def inline(p,text):
    for i,part in enumerate(re.split(r'(`[^`]+`)',text)):
        if part.startswith('`') and part.endswith('`'):
            r=p.add_run(part[1:-1]);r.font.name='Consolas';r.font.size=Pt(9.5)
            r._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'),'Microsoft YaHei')
        else:p.add_run(part)

def table(lines):
    rows=[[v.strip() for v in line.strip().strip('|').split('|')] for line in lines]
    rows=[r for r in rows if not all(re.fullmatch(r'[-: ]+',v) for v in r)]
    n=len(rows[0]);t=d.add_table(rows=0,cols=n);t.alignment=WD_TABLE_ALIGNMENT.CENTER;t.autofit=False
    if n==4:widths=[.78,1.65,2.2,2.23]
    elif '文件' in rows[0][0]:widths=[2.05,2.31,2.5] if n==3 else [2.35,4.51]
    elif n==3:widths=[1.4,2.9,2.56]
    else:widths=[6.86/n]*n
    for col,w in zip(t.columns,widths):col.width=Inches(w)
    for ri,values in enumerate(rows):
        if len(values)!=n:raise ValueError(values)
        cells=t.add_row().cells
        for ci,(cell,value) in enumerate(zip(cells,values)):
            cell.width=Inches(widths[ci]);cell.vertical_alignment=WD_CELL_VERTICAL_ALIGNMENT.CENTER
            tcPr=cell._tc.get_or_add_tcPr()
            shade=OxmlElement('w:shd');shade.set(qn('w:fill'),'DCE6EF' if ri==0 else ('F5F7F9' if ri%2==0 else 'FFFFFF'));tcPr.append(shade)
            margins=OxmlElement('w:tcMar')
            for name,v in [('top',90),('bottom',90),('left',100),('right',100)]:
                e=OxmlElement('w:'+name);e.set(qn('w:w'),str(v));e.set(qn('w:type'),'dxa');margins.append(e)
            tcPr.append(margins)
            p=cell.paragraphs[0];p.paragraph_format.line_spacing=1.15;p.paragraph_format.space_after=Pt(1)
            p.paragraph_format.keep_with_next=(ri==0 or (len(rows)<=5 and ri<len(rows)-1))
            inline(p,value)
            for r in p.runs:r.font.size=Pt(9.5);r.bold=(ri==0)
        trpr=t.rows[-1]._tr.get_or_add_trPr()
        cant=OxmlElement('w:cantSplit');trpr.append(cant)
        if ri==0:
            repeat=OxmlElement('w:tblHeader');trpr.append(repeat)
    borders=OxmlElement('w:tblBorders')
    for name in ['top','left','bottom','right','insideH','insideV']:
        e=OxmlElement('w:'+name);e.set(qn('w:val'),'single');e.set(qn('w:sz'),'4');e.set(qn('w:color'),'D9D9D9');borders.append(e)
    t._tbl.tblPr.append(borders)
    d.add_paragraph().paragraph_format.space_after=Pt(2)

lines=SRC.read_text(encoding='utf-8').splitlines();i=0
while i<len(lines):
    text=lines[i].strip()
    if not text:i+=1;continue
    if text.startswith('|'):
        block=[]
        while i<len(lines) and lines[i].strip().startswith('|'):block.append(lines[i]);i+=1
        table(block);continue
    if text.startswith('```'):
        block=[];i+=1
        while i<len(lines) and not lines[i].startswith('```'):block.append(lines[i]);i+=1
        for line in block:
            p=d.add_paragraph();p.paragraph_format.space_after=Pt(2);p.paragraph_format.line_spacing=1.1
            r=p.add_run(line);r.font.name='Consolas';r.font.size=Pt(9)
        i+=1;continue
    if text=='【整体框架图】':
        p=d.add_paragraph();p.paragraph_format.keep_with_next=True
        p.add_run().add_picture(str(diagram()),width=Inches(6.8))
        cap=d.add_paragraph('图 1  硬件连接与主要软件职责');cap.alignment=WD_ALIGN_PARAGRAPH.CENTER
        cap.runs[0].font.size=Pt(9);i+=1;continue
    if text.startswith('# '):d.add_paragraph(text[2:],'Title')
    elif text.startswith('## '):d.add_heading(text[3:],1)
    elif text.startswith('### '):d.add_heading(re.sub(r'^\d+\.\d+\s+','',text[4:]),2)
    else:
        p=d.add_paragraph();inline(p,text)
    i+=1
d.core_properties.title='分布式声波测距系统答辩说明'
d.core_properties.subject='实现原理 代码导读 调试复盘和答辩准备'
d.core_properties.author='项目组'
d.save(DEST)
print(str(DEST));print('paragraphs',len(d.paragraphs),'tables',len(d.tables))
