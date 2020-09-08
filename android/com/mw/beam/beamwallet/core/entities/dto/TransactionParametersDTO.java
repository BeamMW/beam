package com.mw.beam.beamwallet.core.entities.dto;

import java.util.*; 

public class TransactionParametersDTO
{
    public String address;
    public String identity;

    public BOOL isPermanentAddress;
    public BOOL isOffline;
    public BOOL isMaxPrivacy;
    public long amount;
    public BOOL versionError;
    public String version;
}