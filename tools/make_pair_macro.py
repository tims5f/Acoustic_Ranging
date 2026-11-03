from make_history_macro import address, root

output = str(root / "tools/pair_samples.csv").replace("\\", "\\\\")
macro = f'''execUserAttach()
{{
  __var f;
  __var i;
  __message "pair_captured=", __readMemory8(0x{address('debug_pair_captured'):x}, "Memory"), "\\n";
  if (__readMemory8(0x{address('debug_pair_captured'):x}, "Memory") == 0) return;
  f = __openFile("{output}", "w");
  for (i=0; i<3072; i++)
    __fmessage f, __readMemory16(0x{address('debug_local_pcm'):x}+i*2,"Memory"), ",", __readMemory16(0x{address('debug_peer_pcm'):x}+i*2,"Memory"), "\\n";
  __closeFile(f);
}}
'''
(root / "tools/inspect_pair.mac").write_text(macro)
(root / "tools/reset_pair.mac").write_text(
    'execUserAttach()\n{\n  __writeMemory8(0, 0x%x, "Memory");\n}\n' % address('debug_pair_captured')
)
