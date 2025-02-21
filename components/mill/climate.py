import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart, network

DEPENDENCIES = ["uart", "climate", "network"]


from esphome.components.climate import (
    ClimateMode,
    CONF_CURRENT_TEMPERATURE,
)

mill_ns = cg.esphome_ns.namespace("mill")
MillClimate = mill_ns.class_("MillClimate", uart.UARTDevice, climate.Climate, cg.Component)

CONF_ID = "id"
CONF_uart_id = "uart_id"

CONFIG_SCHEMA = cv.All(
    climate.CLIMATE_SCHEMA.extend(
        {
        cv.GenerateID(): cv.declare_id(MillClimate),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await climate.register_climate(var, config)
