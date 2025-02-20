import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart, network

DEPENDENCIES = ["uart"]

mill_ns = cg.esphome_ns.namespace("mill")
MillClimate = mill_ns.class_("MillClimate", climate.Climate, cg.Component)

CONF_ID = "id"
CONF_uart_id = "uart_id"

CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(MillClimate),
    cv.Required("uart_id"): cv.use_id(uart.UARTComponent),
})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    uart_component = await cg.get_variable(config["uart_id"])
    cg.add(var.set_uart(uart_component))
