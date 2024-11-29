cmd_/home/lll/dynamic-measurement/new/modules.order := {   echo /home/lll/dynamic-measurement/new/memory_reader.ko; :; } | awk '!x[$$0]++' - > /home/lll/dynamic-measurement/new/modules.order
