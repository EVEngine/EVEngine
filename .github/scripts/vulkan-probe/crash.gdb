set pagination off
run
python
if gdb.selected_inferior().pid:
    for command in ("thread apply all bt", "p $_siginfo", "info registers", "x/12i $pc-16", "info proc mappings"):
        gdb.execute(command)
end
