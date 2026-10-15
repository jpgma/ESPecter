#pragma once

class SPIClass {
public:
    /* [SPIClass::begin] O Engine chama begin. Este shim não abre o SPI. */
    void begin() {}
    /* [SPIClass::end] O Engine chama end. Este shim não fecha o SPI. */
    void end() {}
};

inline SPIClass SPI;
