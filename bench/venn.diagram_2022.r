library(nVennR)
# Load data
setwd("P:\\Dev\\nitwit-validator\\bench")
validators = read.csv('./data_2022.csv') # Don't forget to manually add name of first column as "Witness"

# Get intersecting subsets
cpa <- subset(validators, CPAchecker == 0)$Witness
ua <- subset(validators, Ult..Auto. == 0)$Witness
cpaw2t <- subset(validators, CPA.w2t == 0)$Witness
fshell <- subset(validators, FShell.w2t == 0)$Witness
cwv <- subset(validators, NITWIT == 0)$Witness
metaval <- subset(validators, MetaVal == 0)$Witness
symbwitch <- subset(validators, Symb..Witch == 0)$Witness
none <- subset(validators, CPAchecker != 0 & Ult..Auto. != 0 & CPA.w2t != 0 & FShell.w2t != 0 & MetaVal != 0 & NITWIT != 0 & Symb..Witch != 0)$Witness

awesome_venn22 <- plotVenn(list(CPAchecker=cpa, 'Ult. Auto.'=ua,
                              'CPA-w2t'=cpaw2t, 'FShell-w2t'=fshell,
                              NITWIT=cwv,'Symb.-Witch'=symbwitch),
                nCycles = 100000,
                outFile='./output/imgs2022/venn_false.svg')

showSVG(nVennObj = awesome_venn22,
        opacity = 0.2,
        borderWidth = 1,
        fontScale = 0.9,
        labelRegions = T,
        outFile='./output/imgs2022/venn_false_custom.svg')
