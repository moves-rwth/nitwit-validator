# Filtering the witnesses
While I filtered only the violation witnesses I have deleted 76127 files and found 29 erroneous files.
Remained 49594 after that. After also deleting the erroneous ones, 49565 was left.

In witnessInfo remained 125721. So I deleted the non-matching
ones.
76127

Producers:
{'CPAchecker 1.7-svn 29852': 22157, 'Yogar-CBMC': 806, 'verifuzz': 1419, 'ESBMC 6.0.0 kind': 2161, 'CSeq': 1051, 'CPAchecker 1.7-svn b8d6131600+': 2834, 'Taipan': 925, 'Automizer': 1535, 'AProVE': 288, 'DepthK v3.0': 2501, 'CPAchecker 1.7': 424, 'SMACK 1.9.3': 2193, 'DIVINE 4': 1134, 'CBMC': 3002, 'PredatorHP': 247, 'Pinaka': 832, 'Symbiotic': 1451, '2LS': 1429, 'Yogar-CBMC-Parallel': 804, 'VIAP': 112, 'skink': 111, 'Map2Check': 430, 'Kojak': 557, 'VeriAbs': 49, 'Veriabs': 587, ' VeriAbs 1.3 ': 269, 'CPAchecker 1.7-svn 29794': 136, 'CPAchecker 1.6.1-svn 68e5355+': 67, 'AFL': 53}

Witnesses without program file info: 0.
In total C files used: 27831, CIL files used: 21733

Producers for C files:
    {'CPAchecker 1.7-svn 29852': 12210, 'verifuzz': 1238, 'ESBMC 6.0.0 kind': 1009, 'CSeq': 1051, 'CPAchecker 1.7-svn b8d6131600+': 1644, 'Taipan': 513, 'Automizer': 1068, 'DepthK v3.0': 1249, 'CPAchecker 1.7': 365, 'DIVINE 4': 287, 'CBMC': 1239, 'Pinaka': 722, 'Symbiotic': 1131, 'AProVE': 272, '2LS': 1246, 'SMACK 1.9.3': 924, 'skink': 56, 'Map2Check': 196, 'Kojak': 410, 'Veriabs': 587, ' VeriAbs 1.3 ': 269, 'VIAP': 49, 'AFL': 53, 'PredatorHP': 43}
    
Just for Reachability properties (C files):
    {'CPAchecker 1.7-svn 29852': 8539, 'ESBMC 6.0.0 kind': 839, 'CSeq': 1051, 'CPAchecker 1.7-svn b8d6131600+': 931, 'DepthK v3.0': 1081, 'CPAchecker 1.7': 365, 'DIVINE 4': 255, 'CBMC': 898, 'Symbiotic': 950, '2LS': 503, 'verifuzz': 1108, 'SMACK 1.9.3': 763, 'Pinaka': 580, 'skink': 56, 'Map2Check': 61, 'Veriabs': 587, ' VeriAbs 1.3 ': 269, 'AFL': 53, 'PredatorHP': 15}
    
   That's in total 18904 witness files. Of which 4607 are unique (by hash), by filename 4574.
   
   Witnesses for non SV-Benchmark files: 2382 (don't contain sv-benchmark/c/ in program file name).
   Remains: 16522 witnesses, in total unique 3044.
   
   From those 2382 that don't contain sv-benchmark/c/ in the program file name, 2380 are unique.
   That means that there are still some that quite surely correspond to SV-Benchmark programs, but
   have a different name.