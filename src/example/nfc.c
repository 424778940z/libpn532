#include "nfc.h"

#include <stdarg.h>

static const uint32_t NFC_SPI_TIMEOUT = 0x0000ffff;
static bool nfc_powered_on = false;
static PN532_INTERFACE _pn532_interface; // the actuall handle

static void nfc_lowlevel_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOJ_CLK_ENABLE();

    // IRQ    NFC_IRQ      PC4
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = 0; // ignored
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // RSTPDn    NFC_RST      PD5
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = 0; // ignored
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5, GPIO_PIN_SET);
}

static bool nfc_lowlevel_init()
{
    return spi_init_by_device(SPI_NFC);
}

static bool nfc_lowlevel_deinit()
{
    return spi_deinit_by_device(SPI_NFC);
}

static void nfc_lowlevel_reset_ctl(bool ctl)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5, (ctl ? GPIO_PIN_SET : GPIO_PIN_RESET));
}

static void nfc_lowlevel_chip_sel_ctl(bool ctl)
{
    HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_4, (ctl ? GPIO_PIN_SET : GPIO_PIN_RESET));
}

static bool nfc_lowlevel_irq_get()
{
    return HAL_GPIO_ReadPin(GPIOJ, GPIO_PIN_4) == GPIO_PIN_SET;
}

static bool nfc_lowlevel_spi_rw(uint8_t* w_data, uint16_t w_count, uint8_t* r_data, uint16_t r_count)
{
    bool result = true;

    nfc_lowlevel_chip_sel_ctl(false);
    HAL_Delay(1);

    if ( w_data != NULL )
        if ( HAL_SPI_Transmit(&spi_handle_nfc, w_data, w_count, NFC_SPI_TIMEOUT) != HAL_OK )
            result = false;
    if ( r_data != NULL )
        if ( HAL_SPI_Receive(&spi_handle_nfc, r_data, r_count, NFC_SPI_TIMEOUT) != HAL_OK )
        {
            if ( spi_handle_nfc.ErrorCode != HAL_SPI_ERROR_TIMEOUT )
            {
                result = false;
            }
        }

    HAL_Delay(1);
    nfc_lowlevel_chip_sel_ctl(true);

    return result;
}

static void nfc_lowlevel_delay_ms(uint16_t timeout)
{
    HAL_Delay(timeout);
}

void nfc_log(const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    char buf[256] = {0};
    int len = vsnprintf(buf, sizeof(buf), fmt, va);
    display_print(buf, len); // implement your own!
    va_end(va);
}

void nfc_init(void)
{
    nfc_lowlevel_gpio_init();

    _pn532_interface.bus_init = nfc_lowlevel_init;
    _pn532_interface.bus_deinit = nfc_lowlevel_deinit;
    _pn532_interface.reset_ctl = nfc_lowlevel_reset_ctl;
    _pn532_interface.chip_sel_ctl = nfc_lowlevel_chip_sel_ctl;
    _pn532_interface.irq_read = nfc_lowlevel_irq_get;
    _pn532_interface.rw = nfc_lowlevel_spi_rw;
    _pn532_interface.delay_ms = nfc_lowlevel_delay_ms;
    _pn532_interface.log = nfc_log;

    if ( !PN532_InterfaceSetup(&_pn532_interface) )
    {
        // this should never happen as long as above filled all members of
        // pn532_interface
        nfc_lowlevel_log("PN532_InterfaceSetup failed!");
        return;
    }

    PN532_LibrarySetup();
}

static NFC_STATUS nfc_param_config()
{
    NFC_STATUS result = NFC_STATUS_OPERACTION_FAILED;

    do
    {
        // sam config
        if ( !pn532->SAMConfiguration(PN532_SAM_Normal, 0x14, true) )
            break;

        // rf timeouts
        // uint8_t rf_timing[] = {0x00, 0x0b, 0x0a};
        // if ( !pn532->RFConfiguration(PN532_RFConfiguration_CfgItem_VariousTimings, rf_timing, sizeof(rf_timing)) )
        //     break;

        result = NFC_STATUS_OPERACTION_SUCCESS;

        break;
    }
    while ( false );

    return result;
}

NFC_STATUS nfc_pwr_ctl(bool on_off)
{
    // sanity check
    if ( pn532 == NULL )
    {
        return NFC_STATUS_NOT_INITIALIZED;
    }

    NFC_STATUS result = NFC_STATUS_UNDEFINED_ERROR;

    // switch nfc
    if ( on_off )
    {
        pn532->PowerOn();
        result = nfc_param_config();
    }
    else
    {
        pn532->PowerOff();
        result = NFC_STATUS_OPERACTION_SUCCESS;
    }

    // update state
    if ( result == NFC_STATUS_OPERACTION_SUCCESS )
    {
        nfc_powered_on = on_off;
    }

    return result;
}

NFC_STATUS nfc_poll_card(void)
{
    // sanity check
    if ( pn532 == NULL || !nfc_powered_on )
    {
        return NFC_STATUS_NOT_INITIALIZED;
    }

    NFC_STATUS result = NFC_STATUS_UNDEFINED_ERROR;

    // InListPassiveTarget
    PN532_InListPassiveTarget_Params ILPT_params = {
        .MaxTg = 1,
        .BrTy = PN532_InListPassiveTarget_BrTy_106k_typeA,
        .InitiatorData_len = 0,
    };
    PN532_InListPassiveTarget_Results ILPT_result = {0};

    // detect card
    if ( pn532->InListPassiveTarget(ILPT_params, &ILPT_result) )
    {
        // detected
        // only allow a single card
        if ( ILPT_result.NbTg == 1 )
        {
            result = NFC_STATUS_OPERACTION_SUCCESS;
        }
        else
        {
            result = NFC_STATUS_OPERACTION_FAILED;
        }
    }

    return result;
}

NFC_STATUS nfc_wait_card(uint32_t timeout_ms)
{
    // sanity check
    if ( pn532 == NULL || !nfc_powered_on )
    {
        return NFC_STATUS_NOT_INITIALIZED;
    }
    if ( timeout_ms < NFC_TIMEOUT_COUNT_STEP_MS )
    {
        return NFC_STATUS_INVALID_USAGE;
    }

    NFC_STATUS result = NFC_STATUS_UNDEFINED_ERROR;

    // InListPassiveTarget
    PN532_InListPassiveTarget_Params ILPT_params = {
        .MaxTg = 1,
        .BrTy = PN532_InListPassiveTarget_BrTy_106k_typeA,
        .InitiatorData_len = 0,
    };
    PN532_InListPassiveTarget_Results ILPT_result = {0};

    uint32_t time_passed_ms = 0;
    while ( true )
    {
        // detect card
        if ( pn532->InListPassiveTarget(ILPT_params, &ILPT_result) )
        {
            // detected
            // only allow a single card
            if ( ILPT_result.NbTg == 1 )
            {
                result = NFC_STATUS_OPERACTION_SUCCESS;
            }
            else
            {
                result = NFC_STATUS_OPERACTION_FAILED;
            }
            break;
        }

        if ( time_passed_ms < timeout_ms )
        {
            hal_delay(NFC_TIMEOUT_COUNT_STEP_MS);
            time_passed_ms += NFC_TIMEOUT_COUNT_STEP_MS;
            continue;
        }
        else
        {
            result = NFC_STATUS_OPERACTION_TIMEOUT;
            break;
        }
    }

    return result;
}

NFC_STATUS nfc_send_recv(uint8_t* send, uint16_t sendLength, uint8_t* response, uint16_t* responseLength)
{
    // sanity check
    if ( pn532 == NULL || !nfc_powered_on )
    {
        return NFC_STATUS_NOT_INITIALIZED;
    }
    if ( sendLength > PN532_InDataExchange_BUFF_SIZE )
    {
        return NFC_STATUS_INVALID_USAGE;
    }

    NFC_STATUS result = NFC_STATUS_UNDEFINED_ERROR;

    // InDataExchange
    uint8_t InDataExchange_status = 0xff;

    if ( pn532->InDataExchange(1, send, sendLength, &InDataExchange_status, response, responseLength) )
    {
        if ( InDataExchange_status == 0x00 )
        {
            result = NFC_STATUS_OPERACTION_SUCCESS;
        }
        else
        {
            result = NFC_STATUS_OPERACTION_FAILED;
        }
    }

    return result;
}

NFC_STATUS
nfc_send_recv_aio(uint8_t* send, uint16_t sendLength, uint8_t* response, uint16_t* responseLength, uint32_t timeout_ms)
{
    // sanity check
    if ( pn532 == NULL || !nfc_powered_on )
    {
        return NFC_STATUS_NOT_INITIALIZED;
    }
    if ( sendLength > PN532_InDataExchange_BUFF_SIZE || timeout_ms < NFC_TIMEOUT_COUNT_STEP_MS )
    {
        return NFC_STATUS_INVALID_USAGE;
    }

    NFC_STATUS result = NFC_STATUS_UNDEFINED_ERROR;

    // InListPassiveTarget
    PN532_InListPassiveTarget_Params ILPT_params = {
        .MaxTg = 1,
        .BrTy = PN532_InListPassiveTarget_BrTy_106k_typeA,
        .InitiatorData_len = 0,
    };
    PN532_InListPassiveTarget_Results ILPT_result = {0};
    // InDataExchange
    uint8_t InDataExchange_status = 0xff;

    uint32_t time_passed_ms = 0;
    while ( true )
    {
        // detect card
        if ( pn532->InListPassiveTarget(ILPT_params, &ILPT_result) )
        {
            // found card

            if ( ILPT_result.NbTg == 1 )
            {

                if ( pn532->InDataExchange(1, send, sendLength, &InDataExchange_status, response, responseLength) )
                {
                    if ( InDataExchange_status == 0x00 )
                    {
                        result = NFC_STATUS_OPERACTION_SUCCESS;
                    }
                    else
                    {
                        result = NFC_STATUS_OPERACTION_FAILED;
                    }
                }
            }
            else
            {
                result = NFC_STATUS_INVALID_USAGE;
            }
            break;
        }

        if ( time_passed_ms < timeout_ms )
        {
            hal_delay(NFC_TIMEOUT_COUNT_STEP_MS);
            time_passed_ms += NFC_TIMEOUT_COUNT_STEP_MS;
            continue;
        }
        else
        {
            result = NFC_STATUS_OPERACTION_TIMEOUT;
            break;
        }
    }

    return result;
}
