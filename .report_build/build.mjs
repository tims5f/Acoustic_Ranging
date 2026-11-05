import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {Presentation, PresentationFile} from '@oai/artifact-tool';

const root='C:/Users/bjtuj/Desktop/final_work';
const skill='C:/Users/bjtuj/.codex/plugins/cache/openai-primary-runtime/presentations/26.1026.10807/skills/presentations';
const out=path.join(root,'output','工业控制柜微环境监测_课堂报告.pptx');
const p=Presentation.create({slideSize:{width:1280,height:720}});
const C={bg:'#F6F7F4',ink:'#152A33',muted:'#50636B',accent:'#147D74',line:'#C9D5D1',red:'#B44937'};
function box(s,x,y,w,h,t,size=26,bold=false,color=C.ink){let q=s.shapes.add({geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{style:'solid',fill:'none',width:0}});q.text=t;q.text.style={fontFamily:'Microsoft YaHei',fontSize:size,bold,color,verticalAlignment:'middle',wrap:true};return q}
function line(s,x,y,w,color=C.line){s.shapes.add({geometry:'rect',position:{left:x,top:y,width:w,height:2},fill:color,line:{style:'solid',fill:'none',width:0}})}
function base(title,n){let s=p.slides.add();s.background.fill=C.bg;box(s,60,38,1160,66,title,35,true);line(s,60,112,1160);box(s,60,674,950,25,'工业控制柜微环境监测  ·  讨论题 1',15,false,C.muted);box(s,1160,674,60,25,String(n).padStart(2,'0'),15,false,C.muted);return s}
function two(title,leftHead,left,rightHead,right,n,source=''){let s=base(title,n);box(s,62,155,530,48,leftHead,25,true,C.accent);box(s,664,155,530,48,rightHead,25,true,C.accent);box(s,62,218,530,410,left,23);box(s,664,218,530,410,right,23);if(source)box(s,62,638,1120,25,source,13,false,C.muted);return s}
let s=p.slides.add();s.background.fill=C.ink;box(s,72,128,1050,145,'工业电气控制柜\n微环境监测与异常预警',52,true,'#FFFFFF');box(s,76,355,970,80,'温度 · 湿度 · 气压的传感原理、系统设计与多参数判别',27,false,'#A7D9D1');box(s,76,571,900,35,'传感器课程讨论题 1  |  课堂报告',19,false,'#FFFFFF');
two('场景与诊断目标','三类风险','局部发热：接触器、变频器、电源附近温度高于柜内平均值。\n\n凝露：冷表面温度接近或低于空气露点。\n\n密封异常：门缝或密封条改变柜内外空气交换。','三类输出','超温：报告最高测点和持续时间。\n\n凝露：报告露点裕量与高湿暴露。\n\n门/密封：报告压差变化，并结合门磁与工况。',2);
two('传感原理与主要误差','温度 + 湿度','温度：SHT4x 数字温度或 PT100。PT100 电阻随温度变化，适合独立监测热表面。\n\n湿度：高分子电容吸湿后介电常数变化；高湿长期暴露会产生可恢复正偏移。','气压','MEMS 压力芯片测量绝对气压；柜内外两只同型号器件取差 Δp=p内−p外。\n\n温漂、零点偏差、安装高度与风压都会影响 Δp。',3,'资料：IEC 60751:2022；Sensirion SHT4x 数据手册与高湿应用说明；Bosch BMP390 数据手册');
two('采集终端与布点','柜内多点','T/RH：上、中、下 3 点，避开发热器件直吹与风道出口。\n\n热表面：在易凝露的柜壁或金属底板加贴片温度测点。\n\n柜外：同高度布置 T/RH 与气压参考点。','通信与防护','传感器 I²C 接 MCU；长线分布式节点用 RS‑485 回传。\n\n电源与模拟输入做去耦、RC 滤波及浪涌防护。\n\n透气防尘膜防油污，保留通气并安排定期校验。',4);
let a=base('系统架构',5);box(a,70,170,1110,54,'多点传感节点  →  采集 MCU  →  边缘判别  →  本地声光 / SCADA',30,true,C.accent);line(a,70,245,1100);box(a,80,285,1100,300,'输入：T上 / T中 / T下、RH、T冷表面、p内、p外、门磁、风机状态\n\n处理：CRC / 越界检查 → 中值滤波 → 温度补偿 → 露点与压差特征\n\n输出：分级告警、时间戳、原始值、趋势与设备运行状态',27);box(a,80,622,1090,30,'建议采样周期 5–10 s；报警持续时间与阈值在现场试运行中标定。',18,false,C.muted);
two('超温：最大值与分区平均并用','空间统计','风险判定看最高点 T_max，不用平均值掩盖热点。\n\n趋势展示可用加权平均 T̄=ΣwᵢTᵢ，权重按元器件重要性与区域体积设置。','判别逻辑','预警：T_max 超过设备允许环境温度的预警线并持续 N 次。\n\n故障：T_max 越上限，或相同负载下升温速率异常。\n\n检修后用热成像核对测点覆盖。',6,'阈值与 N 为项目参数；设备温度上限应取实际器件规格书。');
two('凝露：以冷表面和露点比较','露点计算','空气温度 T、相对湿度 RH 计算露点 T_d。\n\nγ=ln(RH/100)+17.67T/(243.5+T)\nT_d=243.5γ/(17.67−γ)\n\n露点裕量 M=T冷表面−T_d。','报警与误差','M 接近 0 °C 且持续下降时预警；M≤0 °C 表示具备结露条件。\n\n温湿度必须同位置、同时间采集；冷表面测点决定实际风险。\n\n传感器结露后读数可能失准，需标记数据质量。',7,'露点公式依据 NOAA/NWS 饱和水汽压关系；0 °C 是物理边界，预警裕量需现场校准。');
two('密封：压差只能在可测条件下诊断','压差特征','Δp=p内−p外，比较门前稳定基线与变化率 dΔp/dt。\n\n开门：门磁动作与压差快速回到环境基线相吻合。','判别边界','自然通风柜通常近似等压，密封条破损未必形成可测压差。\n\n若柜体保持微正压，可在稳定风机工况下做压差衰减测试。\n\n风机、空调、车间门动作要纳入工况过滤。',8,'工程推论：气压传感器规格来自 Bosch BMP390；密封诊断需现场标定与门磁佐证。');
two('多参数融合与误报抑制','状态规则','凝露高风险：露点裕量小 + 高湿持续 + 冷表面降温。\n\n散热故障：热点温升 + 风机运行异常。\n\n开门：门磁触发 + 压差突变 + 湿度趋向柜外。','质量控制','使用内外同步采样、同型号压力传感器与零点校准。\n\n中值滤波去尖峰；持续时间、回差、冷却时间防抖动。\n\n单传感器失效时降级并显示“数据不可信”。',9);
let z=base('结论与课堂讨论',10);box(z,74,160,1110,325,'1  多点温度用于发现热点；平均温度只用于趋势。\n\n2  凝露用“冷表面温度 − 露点”判别，单一湿度阈值不够。\n\n3  压差要有可测的工况基线，并由门磁和运行状态佐证。',27);box(z,74,540,1080,90,'讨论：若控制柜没有微正压，能否仅凭两只绝对气压传感器可靠检出密封条老化？还需要补充什么传感量或主动测试？',23,true,C.accent);
let q=base('资料来源',11);box(q,74,155,1130,450,'[1] IEC 60751:2022, Industrial platinum resistance thermometers.\nhttps://webstore.iec.ch/en/publication/63753\n\n[2] Sensirion, SHT4x datasheet and high-humidity application note.\nhttps://sensirion.com/resource/datasheet/sht4x\nhttps://sensirion.com/media/documents/A88858C9/629626D4/Application_Note_Creep_Mitigation_SHT4x.pdf\n\n[3] Bosch Sensortec, BMP390 product page and datasheet.\nhttps://www.bosch-sensortec.com/en/products/environmental-sensors/pressure-sensors/bmp390/\n\n[4] NOAA National Weather Service, relative humidity and dew point calculation.\nhttps://www.weather.gov/media/epz/wxcalc/rhTdFromWetBulb.pdf',18);
await fs.mkdir(path.join(root,'.report_build'),{recursive:true});await fs.mkdir(path.join(root,'output'),{recursive:true});
const draft=path.join(root,'.report_build','candidate.pptx');await (await PresentationFile.exportPptx(p)).save(draft);
const {finalizePresentation}=await import(pathToFileURL(path.join(skill,'container_tools/artifact_tool_utils.mjs')).href);
const result=await finalizePresentation({workspaceDir:root,candidatePath:draft,finalPath:out,pythonExecutable:'C:/Users/bjtuj/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-heading-fit'],fontPolicy:{basis:'design',families:['Aptos']},verifyArtifactToolImport:true,receiptPath:path.join(root,'.report_build','validation.json')});
console.log(JSON.stringify({out,result}));

