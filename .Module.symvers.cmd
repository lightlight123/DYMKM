cmd_/home/lll/dynamic-measurement/new/Module.symvers := sed 's/\.ko$$/\.o/' /home/lll/dynamic-measurement/new/modules.order | scripts/mod/modpost -m -a  -o /home/lll/dynamic-measurement/new/Module.symvers -e -i Module.symvers   -T -
