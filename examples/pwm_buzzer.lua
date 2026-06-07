pwm.setup(1, 2000)
pwm.write(1, 128)

delay.ms(200)

pwm.stop(1)
