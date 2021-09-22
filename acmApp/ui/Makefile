TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

INSTALL_DB = $(INSTALL_LOCATION)/opi

DB += acm_expert.opi
DB += _acm_reg.opi
DB += acm_registers.opi

#----------------------------------------------------
# If <anyname>.db template is not named <anyname>*.template add
# <anyname>_template = <templatename>

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

ifdef T_A

acm_registers.opi$(DEP): $(COMMON_DIR)/acm_registers.opi
	@$(RM) $@
	@echo "$(COMMONDEP_TARGET): ../Makefile" >> $@

$(COMMON_DIR)/acm_registers.opi: ../acm_reg.opi ../gen.py
	$(PYTHON) ../gen.py $@

$(INSTALL_DB)/%.opi: $(COMMON_DIR)/%.opi
	$(ECHO) "Installing created db file $@"
	@$(INSTALL) -d -m $(INSTALL_PERMISSIONS) $< $(@D)

endif
