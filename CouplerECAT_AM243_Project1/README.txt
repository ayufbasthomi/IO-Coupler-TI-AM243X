Add protocol macro
-DACTIVE_PROTOCOL=2

Add LWIP_SUPPORT macro
-DLWIP_SUPPORT

-I"${workspace_loc:/${ProjName}/canopenmaster/inc}" 
-I"${workspace_loc:/${ProjName}/coupler_master}" 
-I"${workspace_loc:/${ProjName}}"  
-I"${workspace_loc:/${ProjName}/driver}" 

-I"${workspace_loc:/${ProjName}}/device_profiles/webserver" -I"${workspace_loc:/${ProjName}}/common" -I"${workspace_loc:/${ProjName}}/common/os" -I"${workspace_loc:/${ProjName}}/common/os/freertos" -I"${workspace_loc:/${ProjName}}/common/board/am243x-lp" -I"${workspace_loc:/${ProjName}}/common/board/am243x-lp/freertos" 

Original:
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/ethercat_subdevice_demo/device_profiles/webserver" 
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/ethercat_subdevice_demo/common" 
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/ethercat_subdevice_demo/common/os" 
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/ethercat_subdevice_demo/common/os/freertos" 
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/ethercat_subdevice_demo/common/board/am243x-lp" 
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/ethercat_subdevice_demo/common/board/am243x-lp/freertos" 
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/custom_phy/inc" 
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/nvm/app/inc" 
-I"${INDUSTRIAL_COMMUNICATIONS_SDK_PATH}/examples/industrial_comms/nvm/drv/inc"







