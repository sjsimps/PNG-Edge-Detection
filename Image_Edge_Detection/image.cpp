#include "lodepng.h"
#include "image.h"
#include <iostream>
#include<math.h>


Image::Image()
{
	m_width = 0;
	m_height = 0;
	m_decoded = false;
	m_intensity_calculated = false;
}

Image::~Image()
{
	Discard_Image();
}

bool Image::Encode_To_Disk(const char* filename)
{
	//Encode and save image to disk
	unsigned error = true;
	if (m_decoded)
	{
		std::vector<unsigned char> png;

		error = lodepng::encode(png, m_image, m_width, m_height);
		if(!error) lodepng::save_file(png, filename);

		if(error) std::cout << "encoder error " << error << ": "<< lodepng_error_text(error) << std::endl;
	}
	return (!error);
}

bool Image::Decode_From_Disk(const char* filename)
{
	unsigned error = false;
	unsigned width, height;

	if (!m_decoded)
	{
		//Load image from disk and decode
		std::vector<unsigned char> png;

		lodepng::load_file(png, filename);
		error = lodepng::decode(m_image, width, height, png);
		//the pixels are now in the vector "image", 4 bytes per pixel, ordered RGBARGBA...

		if(error)
		{
			std::cout << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
		}
		else
		{
			m_width = width;
			m_height = height;

			//instantiate intensity gradient:
			m_gradient = new Intensity_Gradient*[m_width];
			for(int i = 0; i<m_width; i++)
			{
				m_gradient[i] = new Intensity_Gradient[m_height];
			}
			m_decoded = true;
		}
	}
	return (!error);
}

void Image::Discard_Image()
{
	if (m_decoded)
	{
		for(int x = 0; x<m_width; x++)
		{
			delete[] m_gradient[x];
		}
		delete[] m_gradient;

		m_image.clear();
		m_width = 0;
		m_height = 0;
		m_decoded = false;
		m_intensity_calculated = false;
	}

}

void Image::Get_Pixel(int x, int y, Pixel* pix)
{
	int index = 0;
	index += x*4;
	index += y*m_width*4;

	pix->r = m_image[index];
	pix->g = m_image[index+1];
	pix->b = m_image[index+2];
	pix->a = m_image[index+3];
}

void Image::Set_Pixel(int x, int y, Pixel* pix)
{
	int index = 0;
	index += x*4;
	index += y*m_width*4;

	m_image[index] 	= 	pix->r;
	m_image[index+1]= 	pix->g;
	m_image[index+2]= 	pix->b;
	m_image[index+3]= 	pix->a;
}


void Image::Apply_Gauss_Filter3()
{
	Apply_Gauss_Blur(3);
}

void Image::Apply_Gauss_Filter5()
{
	Apply_Gauss_Blur(5);
}


void Image::Apply_Gauss_Blur(const int kernel_size)
{
	//Kernel size must be an odd number
	int kernel_width = kernel_size >> 1;
	float r,g,b,kernel_val;

	Pixel* index_pix = new Pixel;
	Pixel* new_img[m_width][m_height];

	if (!m_decoded)
	{
		std::cout<<"Must decode image before applying gauss blur!\n";
		throw;
	}

	for (int x = 0; x < m_width; x++)
	{
		for (int y = 0; y < m_height; y++)
		{
			r=0;
			g=0;
			b=0;

			for (int i = -kernel_width; i <= kernel_width; i++)
			{
				for (int j = -kernel_width; j <= kernel_width; j++)
				{
                    if ( (x+i >= 0) && (y+j >= 0) && (x+i < m_width) && (y+j < m_height) )
                    {
                        Get_Pixel(x+i, y+j, index_pix);
                    }
                    else Get_Pixel(x, y, index_pix);

                    if (kernel_size == 5)
                    {
                        kernel_val = gauss_5_kernel[i+kernel_width][j+kernel_width] / 159;
                    }
                    else
                    {
                        kernel_val = gauss_3_kernel[i+kernel_width][j+kernel_width];
                    }

                    r += (index_pix->r * kernel_val);
                    g += (index_pix->g * kernel_val);
                    b += (index_pix->b * kernel_val);
				}
			}
			new_img[x][y] = new Pixel;
			new_img[x][y]->r = (r > 0xFF) ? 0xFF : (unsigned char)r;
			new_img[x][y]->g = (g > 0xFF) ? 0xFF : (unsigned char)g;
			new_img[x][y]->b = (b > 0xFF) ? 0xFF : (unsigned char)b;
			new_img[x][y]->a = 0xFF;
		}
	}

	//The pixels must be set after the convolution has occurred.
	//This is important in order to not impact the results of the convolution.
	for (int x = 0; x < m_width; x++)
	{
		for (int y = 0; y < m_height; y++)
		{
			Set_Pixel(x,y,new_img[x][y]);
			delete new_img[x][y];
		}
	}
	delete index_pix;
}

void Image::Map_Intensity_To_Pixels(Color channel)
{
	int intensity;
	Pixel* index_pix = new Pixel;
	index_pix->a = 255;

	for (int x = 0; x < m_width; x++)
	{
		for (int y = 0; y < m_height; y++)
		{
			intensity = (int)(m_gradient[x][y].intensity);
			intensity = (intensity > 255) ? 255 : intensity;

			if (channel == monochrome)
			{
				index_pix->r = intensity;
				index_pix->g = intensity;
				index_pix->b = intensity;
			}
			else
			{
				index_pix->r = (channel == red) ? intensity : 0;
				index_pix->g = (channel == green) ? intensity : 0;
				index_pix->b = (channel == blue) ? intensity : 0;
			}

			Set_Pixel(x,y,index_pix);
		}
	}

	delete index_pix;
}

void Image::Calculate_Differential_Intensity(Color channel)
{
	//Kernel size must be an odd number
	const int kernel_width = 1;

	float horz_magnitude, vert_magnitude;
	int pixel_val = 0;
	Pixel index_pix;

	if (!m_decoded)
	{
		std::cout<<"Must decode image before calculating the differential intensity!\n";
		throw;
	}

	for (int x = 0; x < m_width; x++)
	{
		for (int y = 0; y < m_height; y++)
		{

			horz_magnitude = 0;
			vert_magnitude = 0;

			for (int i = -kernel_width; i <= kernel_width; i++)
			{
				for (int j = -kernel_width; j <= kernel_width; j++)
				{
					if ( (x+i >= 0) && (y+j >= 0) && (x+i < m_width) && (y+j < m_height) )
					{
						Get_Pixel(x+i, y+j, &index_pix);

						switch (channel)
						{
							case red:
							{
								pixel_val = index_pix.r;
								break;
							}
							case green:
							{
								pixel_val = index_pix.g;
								break;
							}
							case blue:
							{
								pixel_val = index_pix.b;
								break;
							}
							case monochrome:
                            default:
							{
								pixel_val = (index_pix.r + index_pix.g + index_pix.b)/3;
								break;
							}
						}

						horz_magnitude += (pixel_val * horz_kernel[i+kernel_width][j+kernel_width]);
						vert_magnitude += (pixel_val * vert_kernel[i+kernel_width][j+kernel_width]);
					}
				}
			}

			m_gradient[x][y].intensity = sqrt( pow(horz_magnitude,2) + pow(vert_magnitude,2) );

			//vertical magnitude must be negative and the horizontal and vertical
			//must be switched in order to compute the normal to the differential angle
			//m_gradient[x][y].angle = (float)atan2(vert_magnitude, horz_magnitude );
			m_gradient[x][y].angle = (float)atan2(horz_magnitude, (-vert_magnitude) );
		}
	}
	m_intensity_calculated = true;
	Map_Intensity_To_Pixels(channel);
}

float Image::Get_Angular_Kernel_Val(int x, int y, float angle)
{
	double pi = 3.14159265;
	double new_angle = (angle < 0) ? ((angle + pi) * 180 / pi) : (angle * 180 / pi);
	float retval = 0;

	if (new_angle <= 11.25 || new_angle > 168.75 )
	{
		retval = kernel_0d8pi[x][y];
	}
	else if (new_angle <= 33.75 )
	{
		retval = kernel_1d8pi[x][y];
	}
	else if (new_angle <= 56.25 )
	{
		retval = kernel_2d8pi[x][y];
	}
	else if (new_angle <= 78.75 )
	{
		retval = kernel_3d8pi[x][y];
	}
	else if (new_angle <= 101.25 )
	{
		retval = kernel_4d8pi[x][y];
	}
	else if (new_angle <= 123.75 )
	{
		retval = kernel_5d8pi[x][y];
	}
	else if (new_angle <= 146.25 )
	{
		retval = kernel_6d8pi[x][y];
	}
	else if (new_angle <= 168.75 )
	{
		retval = kernel_7d8pi[x][y];
	}

	return retval/4;
}

static float pi = 3.14159265;
float Image::Get_Angular_Similarity(float angle1, float angle2)
{
	float difference = angle1 - angle2;
	difference += (difference > pi) ? -(2*pi) : (difference < -pi) ? (2*pi) : 0 ;

	return cos(difference);
}

void Image::Calculate_Angular_Intensity(Color channel)
{
	const int kernel_width = 1;
	//const int kernel_size = 3;

	float magnitude;
	float kernel_val;
	float angle_similarity;

	Pixel* index_pix = new Pixel;
	float new_intensity[m_width][m_height];

	if (!m_intensity_calculated)
	{
		std::cout<<"Must calculate differential intensity before angular intensity!\n";
		throw;
	}

	for (int x = 0; x < m_width; x++)
	{
		for (int y = 0; y < m_height; y++)
		{

			magnitude = 0;

			for (int i = -kernel_width; i <= kernel_width; i++)
			{
				for (int j = -kernel_width; j <= kernel_width; j++)
				{
					if (x+i >= 0 && y+i >= 0 && x+i < m_width && y+j < m_height)
					{
						kernel_val = Get_Angular_Kernel_Val(i+kernel_width, j+kernel_width, m_gradient[x][y].angle );

						angle_similarity = Get_Angular_Similarity(m_gradient[x+i][y+j].angle, m_gradient[x][y].angle);

						magnitude += (m_gradient[x+i][y+j].intensity * angle_similarity * kernel_val);

					}
				}
			}
			new_intensity[x][y] = (magnitude > 0) ? magnitude : 0;
		}
	}

	for (int x = 0; x < m_width; x++)
	{
		for (int y = 0; y < m_height; y++)
		{
			m_gradient[x][y].intensity = new_intensity[x][y];
		}
	}

	Map_Intensity_To_Pixels(channel);

	delete index_pix;
}

void Image::Apply_Threshold(unsigned int percent)
{
	Pixel white_pix;
	Pixel black_pix;
	white_pix = {.r = 0xFF,	.g = 0xFF,	.b = 0xFF,	.a = 0xFF};
	black_pix = {.r = 0x00,	.g = 0x00,	.b = 0x00,	.a = 0xFF};
	float threshold = Intensity_At_Percentile(percent);

	if (!m_intensity_calculated)
	{
		std::cout<<"Must calculate differential intensity before applying an image threshold!\n";
		throw;
	}

	for (int x = 0; x < m_width; x++)
	{
		for (int y = 0; y < m_height; y++)
		{
			if (m_gradient[x][y].intensity > threshold)
			{
				Set_Pixel(x,y,&white_pix);
			}
			else
			{
				Set_Pixel(x,y, &black_pix);
			}
		}
	}

}

float Image::Intensity_At_Percentile(unsigned int percentile)
{

	int num_entries = m_width*m_height;
	float intensities[num_entries];
	int index = (percentile < 99) ? ((num_entries-1) / 100) * percentile : (num_entries-1);

	int i = 0;
	for (int x = 0; x < m_width; x++)
	{
		for (int y = 0; y < m_height; y++)
		{
			intensities[i] = m_gradient[x][y].intensity;
			i++;
		}
	}

	Merge_Sort(intensities, 0, num_entries-1);

	return intensities[index];
}

void Image::Merge_Sort(float arr[], int left_i, int right_i)
{
	int split_index = (right_i + left_i)/2;
	int num_entries = (right_i - left_i + 1);
	int merge_i1, merge_i2;
	float temp[right_i - left_i + 1];

	if (left_i < right_i)
	{
		Merge_Sort(arr, left_i, split_index);
		Merge_Sort(arr, split_index+1, right_i);

		merge_i1 = left_i;
		merge_i2 = split_index + 1;

		for (int i = 0; i<num_entries; i++)
		{
			if ( ( (merge_i1 <= split_index) && (arr[merge_i1] < arr[merge_i2]) ) || (merge_i2 > right_i))
			{
				temp[i] = arr[merge_i1];
				merge_i1++;
			}
			else
			{
				temp[i] = arr[merge_i2];
				merge_i2++;
			}
		}

		for (int i = 0; i < num_entries; i++)
		{
			arr[left_i + i] = temp[i];
		}
	}
}








