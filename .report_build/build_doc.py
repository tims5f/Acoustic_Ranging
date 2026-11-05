from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn

OUT=r'C:\Users\bjtuj\Desktop\传感器\工业电气控制柜微环境监测系统_课堂报告.docx'
d=Document(); sec=d.sections[0]
sec.page_width=Inches(8.5);sec.page_height=Inches(11)
sec.top_margin=Inches(.78);sec.bottom_margin=Inches(.72);sec.left_margin=Inches(.9);sec.right_margin=Inches(.9)
styles=d.styles
for name in ['Normal','Title','Heading 1','Heading 2']:
 s=styles[name];s.font.name='Microsoft YaHei';s.font.color.rgb=RGBColor(0,0,0)
 s._element.rPr.rFonts.set(qn('w:eastAsia'),'Microsoft YaHei')
styles['Normal'].font.size=Pt(10.5);styles['Normal'].paragraph_format.line_spacing=1.35;styles['Normal'].paragraph_format.space_after=Pt(7)
styles['Title'].font.size=Pt(20);styles['Title'].font.bold=True;styles['Title'].paragraph_format.space_after=Pt(10)
ppr=styles['Title']._element.get_or_add_pPr()
for old in ppr.findall(qn('w:pBdr')): ppr.remove(old)
bdr=OxmlElement('w:pBdr'); bottom=OxmlElement('w:bottom'); bottom.set(qn('w:val'),'nil'); bdr.append(bottom); ppr.append(bdr)
styles['Heading 1'].font.size=Pt(14);styles['Heading 1'].font.bold=True;styles['Heading 1'].paragraph_format.space_before=Pt(15);styles['Heading 1'].paragraph_format.space_after=Pt(6)
styles['Heading 2'].font.size=Pt(11.5);styles['Heading 2'].font.bold=True;styles['Heading 2'].paragraph_format.space_before=Pt(9);styles['Heading 2'].paragraph_format.space_after=Pt(3)
def p(t='',style=None): return d.add_paragraph(t,style)
def h(t): d.add_heading(t,1)
def hh(t): d.add_heading(t,2)
def tbl(headers,rows,widths=None):
 t=d.add_table(rows=1,cols=len(headers));t.alignment=WD_TABLE_ALIGNMENT.CENTER;t.style='Table Grid'
 for i,x in enumerate(headers):t.rows[0].cells[i].text=x
 for row in rows:
  cells=t.add_row().cells
  for i,x in enumerate(row):cells[i].text=x
 for ri,row in enumerate(t.rows):
  for ci,c in enumerate(row.cells):
   c.vertical_alignment=WD_CELL_VERTICAL_ALIGNMENT.CENTER
   if widths:c.width=Inches(widths[ci])
   tcPr=c._tc.get_or_add_tcPr(); sh=OxmlElement('w:shd');sh.set(qn('w:fill'),'DCE8EC' if ri==0 else ('F7F9FA' if ri%2==0 else 'FFFFFF'));tcPr.append(sh)
   for pp in c.paragraphs:
    pp.paragraph_format.space_after=Pt(2)
    for run in pp.runs:run.font.size=Pt(9);run.bold=(ri==0)
 d.add_paragraph()
 return t

d.add_paragraph('工业电气控制柜微环境监测系统课堂报告','Title')
p('讨论题 1  温度 湿度 气压监测与设备柜异常预警')
p('本报告提出一套控制柜在线监测方案：用多点温度识别局部过热，用空气露点与冷表面温度判断凝露风险，在具备稳定压差的工况下结合门磁、风机状态判断开门或密封异常。压差和湿度都不能单独作为故障结论；阈值需要按照设备规格与现场试运行数据标定。')
h('一 监测对象与传感原理')
hh('温度')
p('柜内电源、接触器和变频器等器件形成局部热源，单点温度不能代表柜内最高温。数字温度传感器适合空气测点；需独立监测柜壁或底板冷表面时，可用贴片式铂电阻。PT100 在 0 °C 时标称电阻为 100 Ω，其电阻与温度关系由 IEC 60751 规定。导线电阻会带来测量偏差，长线测量宜使用三线或四线接法。[1]')
hh('湿度')
p('高分子电容式湿度传感器通过聚合物吸收水汽后的介电变化测量相对湿度。SHT4x 一类器件集成温湿度测量、数字处理和 I²C 接口。长期暴露于 90%RH 以上时，水分滞留在聚合物内可造成可恢复的正向偏移；结露、粉尘和油污还会改变响应速度或污染敏感层。因此需要透气防护、异常数据标记和定期检查。[2][3]')
hh('气压')
p('MEMS 气压传感器把膜片受压产生的机械形变转为电信号，并通过芯片内校准参数补偿温度影响。建议柜内外各布置一只相同型号的数字气压传感器，同步采集后计算 Δp＝p内－p外。BMP390 提供 I²C/SPI 接口和温度补偿，但其规格精度不等于整套系统的压差精度；两个器件的零点差、安装位置和气流扰动仍需现场校准。[4]')
d.add_page_break()
h('二 系统架构与安装')
tbl(['层级','设计'],[['感知层','柜内上中下 3 个空气温湿度测点；关键发热器件附近增设温度点；易凝露金属面布置表面温度点；柜外同高度布置温湿度与气压参考点；门磁和风机状态作为辅助输入。'],['采集层','MCU 定时同步采样，检查数字传感器 CRC 和越界值；局部 I²C 接线短而远离强电，长距离节点经 RS‑485 回传。模拟测点加 RC 滤波、电源去耦和浪涌防护。'],['处理与告警','边缘端计算最高温、露点、露点裕量、压差及变化率；本地声光报警，同时向 SCADA 上传原始值、状态和时间戳。'],['防护维护','探头采用透气防尘膜，避免油雾直冲；安装于非直吹位置并保持通气；巡检时清洁、比对和复核零点。']])
p('采样周期可先取 5—10 s，具体由柜内变化速度、总线负载和误报容忍度确定。测点应避开风道出口，但不能远离待诊断的局部热点；热成像巡检可用于核对测点覆盖。')
h('三 误差抑制与异常判别')
hh('超温与温度梯度')
p('超温判断采用各测点最高值 Tmax 和持续时间，避免加权平均掩盖热点。空间加权平均 T̄＝ΣwiTi 可用于趋势展示，权重按区域体积、元器件重要性和测点代表性设置。报警上限应取实际器件规格书允许环境温度，并在相近负载下比较升温速率。对瞬时尖峰可使用中值滤波、连续 N 次越限和回差。')
hh('凝露与露点')
p('先用同位置、同时间的空气温度 T 和相对湿度 RH 计算露点 Td，再比较冷表面温度 Ts。采用常用 Magnus 近似：γ＝ln(RH/100)＋17.67T/(243.5＋T)，Td＝243.5γ/(17.67－γ)，其中温度单位为 °C。定义露点裕量 M＝Ts－Td；M≤0 °C 表示该表面具备结露条件。预警可设在 M 接近 0 °C 且持续下降时，具体安全裕量应覆盖传感器精度与表面温度测量误差。[5]')
p('若湿度传感器自身结露，其读数可能暂时失准。系统应标记高湿饱和期间的数据质量，必要时按器件说明使用短时加热恢复，并避免把加热期间数据用于露点计算。[2][3]')
hh('柜门与密封异常')
p('计算 Δp 的短时变化率与门前稳定基线。门磁动作、Δp 快速趋向外界基线、柜内湿度逐渐接近柜外，可共同支持“开门”判断；在稳定微正压和相同风机工况下，压差持续降低或衰减加快才可提示密封异常。风机启停、车间通风和空调切换期间应暂缓判定。自然通风控制柜往往接近内外等压，单凭两只绝对气压传感器不能可靠检出密封条老化；可补充门磁、风机工况，或采用受控微正压测试。此处属于基于测量原理的工程推论，需在现场验证。')
h('四 多参数融合与实施验证')
tbl(['风险','建议判别组合','抑制误报的方法'],[['过热','Tmax 越限或同负载下升温异常','多点测量、持续时间、与风机状态关联'],['凝露','露点裕量变小＋高湿持续＋冷表面降温','同点同步采样、传感器结露时降低置信度'],['开门','门磁触发＋压差快速变化＋湿度向柜外趋同','排除风机和车间通风切换时段'],['密封失效','稳定微正压工况下压差基线长期下降','校准两只压力传感器，并做受控测试']])
p('实施时先建立正常工况基线，再用开门、停风机、局部加热和冷表面降温等可控试验验证规则。记录误报、漏报和响应时间，并据此调整阈值。传感器失联或读数越界应触发“测量故障”状态，不应被当作环境正常。')
h('五 结论与课堂讨论')
p('多点温度解决局部热点被平均值掩盖的问题；露点与冷表面温度共同决定凝露风险；气压变化只有在工况可控、压差可测时才适合参与密封诊断。多参数融合的价值在于让每个报警都附带独立佐证和适用条件。')
p('讨论问题：如果控制柜没有微正压，仅用柜内外气压数据能否检出密封条老化？若不能，应优先增加门磁、风机状态、表面温度，还是增加主动压差测试？')
h('参考资料')
refs=[
'[1] IEC. IEC 60751:2022 Industrial platinum resistance thermometers and platinum temperature sensors. https://webstore.iec.ch/en/publication/63753',
'[2] Sensirion. SHT4x datasheet. https://sensirion.com/resource/datasheet/sht4x',
'[3] Sensirion. Using the Integrated Heater of SHT4x in High-Humidity Environments. https://sensirion.com/media/documents/A88858C9/629626D4/Application_Note_Creep_Mitigation_SHT4x.pdf',
'[4] Bosch Sensortec. BMP390 pressure sensor product information. https://www.bosch-sensortec.com/en/products/environmental-sensors/pressure-sensors/bmp390/',
'[5] NOAA National Weather Service. Relative Humidity and Dewpoint Temperature calculation. https://www.weather.gov/media/epz/wxcalc/rhTdFromWetBulb.pdf']
for x in refs:p(x)
footer=sec.footer.paragraphs[0];footer.alignment=WD_ALIGN_PARAGRAPH.CENTER;footer.text='传感器课程讨论题 1'
d.save(OUT)
print(OUT)


