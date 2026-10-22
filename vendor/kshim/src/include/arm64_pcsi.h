#pragma once
#include <asm/arm64_smc.h>

enum arm64_pcsi_scm_return_err
{
    PCSI_SUCCESS,
    PCSI_NOT_SUPPORTED = -1,
    PCSI_INVALID_PARAMETER = -2,
    PCSI_DENIED = -3,
    PCSI_ALREADY_ON = -4,
    PCSI_ON_PENDING = -5,
    PCSI_INTERNAL_FAILURE = -6,
    PCSI_NOT_PRESENT = -7,
    PCSI_DISABLED = -8,
    PCSI_INVALID_ADDRESS = -9,
    PCSI_UNKOWN_STATE = 1,
};