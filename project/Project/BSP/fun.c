#include "headfile.h"

uint8_t lcd_face=0;//0监控，1输出配置，2运行参数
uint8_t lcd0_first=1,lcd1_first=1,lcd2_first=1;
uint8_t lcd1_select=0;//0TarD,1TarP
uint8_t lcd2_select=0;//0FH,1FL,2PL,3DH
uint8_t work_mode=0;//0手动，1自动
uint8_t warn_flag=0;
double r37_volt;
double P;//实时压力
uint32_t r39_fre;
double F;//瞬时流量
uint32_t Q=0;//累计流量
double v_offset=0;
uint32_t TarD=5,TarD_temp=5;//pwm目标占空比
uint32_t D=5+1;//实时占空比
double TarP=5.0,TarP_temp=5.0;//目标压力值
double FH=20.0,FH_temp=20.0;
double FL=10.0,FL_temp=10.0;
double PL=1.0,PL_temp=1.0;
uint32_t DH=65,DH_temp=65;
uint32_t D_last=5;

uint32_t lcd_sp;
void lcd_show(void)
{
	if(HAL_GetTick()-lcd_sp<=100)return;
	lcd_sp=HAL_GetTick();
	
	char text[21];
	
	if(lcd_face==0)
	{
		if(lcd0_first==1)
		{
			FH=FH_temp;
			FL=FL_temp;
			PL=PL_temp;
			DH=DH_temp;
			
			lcd2_first=1;
			lcd0_first=0;
		}
		
		sprintf(text,"       MAIN         ");
	  LCD_DisplayStringLine(Line1,(u8*)text);
		sprintf(text,"   M=%s       ",work_mode?"AUTO":"MAN");
	  LCD_DisplayStringLine(Line3,(u8*)text);
		sprintf(text,"   P=%.1fBAR      ",P);
	  LCD_DisplayStringLine(Line4,(u8*)text);
		sprintf(text,"   F=%.1fL/M      ",F);
	  LCD_DisplayStringLine(Line5,(u8*)text);
		sprintf(text,"   Q=%dL          ",Q);
	  LCD_DisplayStringLine(Line6,(u8*)text);
		sprintf(text,"   D=%d%%         ",D);
	  LCD_DisplayStringLine(Line7,(u8*)text);
		sprintf(text,"   V=%.1fV        ",v_offset);
	  LCD_DisplayStringLine(Line8,(u8*)text);
	}
	
	if(lcd_face==1)
	{
		if(lcd1_first==1)
		{
		  lcd1_select=0;
			
			
			lcd0_first=1;
			lcd1_first=0;
		}
		
		
		sprintf(text,"       OUTP         ");
	  LCD_DisplayStringLine(Line1,(u8*)text);
		sprintf(text,"   TarD=%d%%        ",TarD_temp);
	  LCD_DisplayStringLine(Line3,(u8*)text);
		sprintf(text,"   TarP=%.1fBAR     ",TarP_temp);
	  LCD_DisplayStringLine(Line4,(u8*)text);
	}
	
	if(lcd_face==2)
	{
		if(lcd2_first==1)
		{
			lcd2_select=0;
			
			TarD=TarD_temp;
			TarP=TarP_temp;
			
			lcd1_first=1;
			lcd2_first=0;
		}
		
		
		sprintf(text,"       PARA         ");
	  LCD_DisplayStringLine(Line1,(u8*)text);
		sprintf(text,"   FH=%.1fL/M      ",FH_temp);
	  LCD_DisplayStringLine(Line3,(u8*)text);
		sprintf(text,"   FL=%.1fL/M      ",FL_temp);
	  LCD_DisplayStringLine(Line4,(u8*)text);
		sprintf(text,"   PL=%.1fBAR      ",PL_temp);
	  LCD_DisplayStringLine(Line5,(u8*)text);
		sprintf(text,"   DH=%d%%       ",DH_temp);
	  LCD_DisplayStringLine(Line6,(u8*)text);
	}
	
}


uint32_t key_sp;
uint8_t k1_last=1,k2_last=1,k3_last=1,k4_last=1;
uint8_t k3_long=0,k4_long=0;
void key_scan(void)
{
	if(HAL_GetTick()-key_sp<=50)return;
	key_sp=HAL_GetTick();
	
	uint8_t k1_state=HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_0);
	uint8_t k2_state=HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_1);
	uint8_t k3_state=HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_2);
	uint8_t k4_state=HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_0);
	
	if(k1_state==0&&k1_last==1)
	{
		lcd_face=(lcd_face+1)%3;
		LCD_Clear(Black);
	}
	
	if(k2_state==0&&k2_last==1)
	{
		if(lcd_face==0)
		{
			work_mode=!work_mode;
		}
		
		if(lcd_face==1)
		{
			lcd1_select=!lcd1_select;
		}
		
		if(lcd_face==2)
		{
			lcd2_select=(lcd2_select+1)%4;
		}
	}
	
	if(k3_state==0&&k3_last==1)
	{
		if(lcd_face==1)
		{
			if(lcd1_select==0)
			{
				if(TarD_temp>=5&&TarD_temp<95)TarD_temp+=5;
			}else
			{
				if(TarP_temp>=1.0&&TarP_temp<9.5)TarP_temp+=0.5;
			}
		}
		
		if(lcd_face==2)
		{
			if(lcd2_select==0)if(FH_temp>=4&&FH_temp<40)FH_temp++;
			if(lcd2_select==1)if(FL_temp>=4&&FL_temp<40)FL_temp++;
			if(lcd2_select==2)if(PL_temp>=1.0&&PL_temp<9.5)PL_temp+=0.5;
			if(lcd2_select==3)if(DH_temp>=5&&DH_temp<95)DH_temp+=10;
		}
		
	}
	
	if(k4_state==0&&k4_last==1)
	{
		
		if(lcd_face==1)
		{
			if(lcd1_select==0)
			{
				if(TarD_temp>5&&TarD_temp<=95)TarD_temp-=5;
			}else
			{
				if(TarP_temp>1.0&&TarP_temp<=9.5)TarP_temp-=0.5;
			}
		}
		
		if(lcd_face==2)
		{
			if(lcd2_select==0)if(FH_temp>4&&FH_temp<=40)FH_temp--;
			if(lcd2_select==1)if(FL_temp>4&&FL_temp<=40)FL_temp--;
			if(lcd2_select==2)if(PL_temp>1.0&&PL_temp<=9.5)PL_temp-=0.5;
			if(lcd2_select==3)if(DH_temp>5&&DH_temp<=95)DH_temp-=10;
		}
	}
	
	if(lcd_face==0)//监控界面长按功能
		{
			if(k3_state==0&&k3_last==1)//k3_long
			{
				TIM4->CNT=0;
				k3_long=0;
			}else if(k3_state==0&&k3_last==0)
			{
				if(TIM4->CNT>20000)
				{
					v_offset=r37_volt;
				}
				
				k3_long=1;
			}
			
			
			if(k4_state==0&&k4_last==1)//k4_long
			{
				TIM4->CNT=0;
				k4_long=0;
			}else if(k4_state==0&&k4_last==0)
			{
				if(TIM4->CNT>20000)
				{
					Q=0;
				}
				k4_long=1;
			}
		}
	
	k1_last=k1_state;
	k2_last=k2_state;
	k3_last=k3_state;
	k4_last=k4_state;
}

void led_show(uint8_t led,uint8_t mode)
{
	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_8<<(led-1), mode?GPIO_PIN_RESET:GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOD,GPIO_PIN_2,GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOD,GPIO_PIN_2,GPIO_PIN_RESET);
}

uint8_t led2_first=1,led3_first=1;
uint8_t led4_mode=0;

void led_proc(void)
{
	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOD,GPIO_PIN_2,GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOD,GPIO_PIN_2,GPIO_PIN_RESET);
	
	if(r39_fre>8000)warn_flag=1;else warn_flag=0;//led1
	warn_flag?led_show(1,1):led_show(1,0);
	
	if(F<FL&&D>DH)//led2
	{
		if(led2_first)
		{
			TIM1->CNT=0;
			led2_first=0;
		}
		if(TIM1->CNT>20000)
		{
			led_show(2,1);
		}
		
	}else
	{
		led_show(2,0);
		led2_first=1;
	}
	
	if(F>FH&&P<PL)//led3
	{
		if(led3_first)
		{
			TIM17->CNT=0;
			led3_first=0;
		}
		if(TIM17->CNT>20000)
		{
			led_show(3,1);
		}
		
	}else
	{
		led_show(3,0);
		led3_first=1;
	}
	
	led_show(4,led4_mode);
}

void adc_proc(void)
{
	HAL_ADC_Start(&hadc2);
	uint32_t val=HAL_ADC_GetValue(&hadc2);
	r37_volt=3.3*val/4095;
	if(r37_volt>=v_offset)
	{
		P=(r37_volt-v_offset)/(3.3-v_offset)*10.0;
	}else
	{
		P=0;
	}
	
	
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	if(htim->Instance==TIM3)
	{
		uint32_t val=HAL_TIM_ReadCapturedValue(htim,TIM_CHANNEL_1);
		TIM3->CNT=0;
		r39_fre=80000000/(80*val);
		if(r39_fre>=800&&r39_fre<=8000)
		{
			F=(double)(r39_fre/200.0);
		}else if(r39_fre<800)
		{
			F=0;
		}else if(r39_fre>8000)
		{
			F=40;
		}
		
	}
}

double duty;
void pwm_proc(void)
{
	duty=TarD/100.0;
	TIM2->CCR2=(uint32_t)((D/100.0)*(TIM2->ARR+1));
	
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if(htim->Instance==TIM6)
	{
		Q+=(uint32_t)(F-0.5);
	}
	
	if(htim->Instance==TIM7)
	{
		if(work_mode==0)//手动模式下调节占空比
		{
			
			if((D/100.0)>duty)D--;
		  if((D/100.0)<duty)D++;
		}
		else
		{
			if(P>(TarP+0.5))if(D>5&&D<=95)D--;
			if(P<(TarP-0.5))if(D>=5&&D<95)D++;
		}
		if(D_last==D)led4_mode=0;else led4_mode=1;
		D_last=D;
	}
}