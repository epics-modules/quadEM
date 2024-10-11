createParam(PIDX_SpString, asynParamFloat64, &P_PIDXSp);
curr_reg.reg_num = 50;
curr_reg.asyn_num = P_PIDXSp;
curr_reg.pv_min = -1.0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = -10000;
curr_reg.reg_max = 10000;
pidRegData_.push_front(curr_reg);

createParam(PIDX_KpString, asynParamFloat64, &P_PIDXKp);
curr_reg.reg_num = 51;
curr_reg.asyn_num = P_PIDXKp;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 10000;
pidRegData_.push_front(curr_reg);

createParam(PIDX_KiString, asynParamFloat64, &P_PIDXKi);
curr_reg.reg_num = 52;
curr_reg.asyn_num = P_PIDXKi;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 10000;
pidRegData_.push_front(curr_reg);

createParam(PIDX_KdString, asynParamFloat64, &P_PIDXKd);
curr_reg.reg_num = 53;
curr_reg.asyn_num = P_PIDXKd;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 10000;
pidRegData_.push_front(curr_reg);

createParam(PIDX_ScaleString, asynParamFloat64, &P_PIDX_Scale);
curr_reg.reg_num = 54;
curr_reg.asyn_num = P_PIDX_Scale;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 1.0;
pidRegData_.push_front(curr_reg);

createParam(PIDX_VScaleString, asynParamFloat64, &P_PIDX_VScale);
curr_reg.reg_num = 56;
curr_reg.asyn_num = P_PIDX_VScale;
curr_reg.pv_min = -5.0;
curr_reg.pv_max = 5.0;
curr_reg.reg_min = -50000;
curr_reg.reg_max = 50000;
pidRegData_.push_front(curr_reg);

createParam(PIDX_VOffsetString, asynParamFloat64, &P_PIDX_VOffset);
curr_reg.reg_num = 57;
curr_reg.asyn_num = P_PIDX_VOffset;
curr_reg.pv_min = 0;
curr_reg.pv_max = 10.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 100000;
pidRegData_.push_front(curr_reg);

createParam(PIDY_SpString, asynParamFloat64, &P_PIDYSp);
curr_reg.reg_num = 60;
curr_reg.asyn_num = P_PIDYSp;
curr_reg.pv_min = -1.0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = -10000;
curr_reg.reg_max = 10000;
pidRegData_.push_front(curr_reg);

createParam(PIDY_KpString, asynParamFloat64, &P_PIDYKp);
curr_reg.reg_num = 61;
curr_reg.asyn_num = P_PIDYKp;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 10000;
pidRegData_.push_front(curr_reg);

createParam(PIDY_KiString, asynParamFloat64, &P_PIDYKi);
curr_reg.reg_num = 62;
curr_reg.asyn_num = P_PIDYKi;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 10000;
pidRegData_.push_front(curr_reg);

createParam(PIDY_KdString, asynParamFloat64, &P_PIDYKd);
curr_reg.reg_num = 63;
curr_reg.asyn_num = P_PIDYKd;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 10000;
pidRegData_.push_front(curr_reg);

createParam(PIDY_ScaleString, asynParamFloat64, &P_PIDY_Scale);
curr_reg.reg_num = 64;
curr_reg.asyn_num = P_PIDY_Scale;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 1.0;
pidRegData_.push_front(curr_reg);

createParam(PIDY_VScaleString, asynParamFloat64, &P_PIDY_VScale);
curr_reg.reg_num = 66;
curr_reg.asyn_num = P_PIDY_VScale;
curr_reg.pv_min = -5.0;
curr_reg.pv_max = 5.0;
curr_reg.reg_min = -50000;
curr_reg.reg_max = 50000;
pidRegData_.push_front(curr_reg);

createParam(PIDY_VOffsetString, asynParamFloat64, &P_PIDY_VOffset);
curr_reg.reg_num = 67;
curr_reg.asyn_num = P_PIDY_VOffset;
curr_reg.pv_min = 0;
curr_reg.pv_max = 10.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 100000;
pidRegData_.push_front(curr_reg);

createParam(PID_CutoutString, asynParamFloat64, &P_PIDCutout);
curr_reg.reg_num = 90;
curr_reg.asyn_num = P_PIDCutout;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 1000.0;
pidRegData_.push_front(curr_reg);

createParam(PID_HystString, asynParamFloat64, &P_PIDHyst);
curr_reg.reg_num = 91;
curr_reg.asyn_num = P_PIDHyst;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 1000.0;
pidRegData_.push_front(curr_reg);

createParam(DAC_ItoVString, asynParamFloat64, &P_DACItoV);
curr_reg.reg_num = 92;
curr_reg.asyn_num = P_DACItoV;
curr_reg.pv_min = 0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 1.0;
pidRegData_.push_front(curr_reg);

createParam(DAC_ItoVOffsetString, asynParamFloat64, &P_DACItoVOffset);
curr_reg.reg_num = 76;
curr_reg.asyn_num = P_DACItoVOffset;
curr_reg.pv_min = 0;
curr_reg.pv_max = 10.0;
curr_reg.reg_min = 0;
curr_reg.reg_max = 100000.0;
pidRegData_.push_front(curr_reg);

createParam(PosTrackRadString, asynParamFloat64, &P_PosTrackRad);
curr_reg.reg_num = 20;
curr_reg.asyn_num = P_PosTrackRad;
curr_reg.pv_min = -1.0;
curr_reg.pv_max = 1.0;
curr_reg.reg_min = -10000.0;
curr_reg.reg_max = 10000.0;
pidRegData_.push_front(curr_reg);

