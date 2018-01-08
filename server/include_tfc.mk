MCPPP=/usr/local/monitor_dev/public_64/tfc/

LIBHTTP=$(MCPPP)libhttp/
CCD=$(MCPPP)ccd/
MCD=$(MCPPP)mcd/
OLD=$(MCPPP)old/
BASE=$(MCPPP)base/
DCC=$(MCPPP)dcc/
WATCHDOG=$(MCPPP)watchdog/
WTG=$(MCPPP)wtg/


INC=-I$(LIBHTTP) -I$(CCD) -I$(DCC) -I$(MCD) -I$(OLD) -I$(BASE) -I$(WATCHDOG) -I$(WTG)
OBJ= $(LIBHTTP)http_api.o  \
	 $(LIBHTTP)tfc_base_http.o  \
	 $(BASE)tfc_base_config_file.o  \
	 $(BASE)tfc_base_timer.o  \
	 $(CCD)tfc_net_ipc_mq.o
